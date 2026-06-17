/*
 * mm/mmap.c - 用户地址空间管理
 *
 * 功能：
 *   管理用户进程的虚拟地址空间。mm_struct 包含 pgd、brk、代码段范围
 *   以及 vma[NR_VMA] 固定数组。每个 vm_area_struct 描述一段连续虚拟区域。
 *
 * 主要函数：
 *   mm_alloc()           - 分配并初始化 mm_struct
 *   mm_destroy()         - 销毁用户地址空间
 *   mm_create_user_pgd() - 创建用户页表 + 复制内核映射 + 映射 MMIO
 *   find_vma()           - 查找包含指定地址的 VMA
 *   mm_brk()             - brk 内部实现（lazy allocation，不缩小）
 */

#include <kernel/mm.h>
#include <kernel/errno.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/slab.h>
#include <kernel/buddy.h>
#include <kernel/signal.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/csr.h>
#include <drivers/uart.h>
#include <drivers/virtio.h>

/* ---- 内部辅助函数 ---- */

/*
 * vma_alloc_slot - 在 mm->vma[] 中找一个空闲槽位
 *
 * 线性扫描，返回空闲槽位指针，满则返回 NULL。
 */
static struct vm_area_struct *vma_alloc_slot(struct mm_struct *mm)
{
	for (int i = 0; i < NR_VMA; i++) {
		if (!mm->vma[i].used)
			return &mm->vma[i];
	}
	return NULL;
}

static void vma_free_slot(struct vm_area_struct *vma)
{
	memset(vma, 0, sizeof(*vma));
}

static uintptr_t align_up_page(uintptr_t addr)
{
	return (addr + PAGE_SIZE - 1) & PAGE_MASK;
}

static bool range_overflows(uintptr_t start, size_t length, uintptr_t *end)
{
	uintptr_t aligned_len;

	if (length > TASK_SIZE)
		return true;

	aligned_len = align_up_page(length);

	if (aligned_len == 0 || start + aligned_len < start)
		return true;

	*end = start + aligned_len;
	return false;
}

static bool vma_overlaps(struct vm_area_struct *vma, uintptr_t start,
			 uintptr_t end)
{
	return vma->used && start < vma->vm_end && end > vma->vm_start;
}

static bool range_overlaps_other_vma(struct mm_struct *mm,
				     struct vm_area_struct *skip,
				     uintptr_t start, uintptr_t end)
{
	for (int i = 0; i < NR_VMA; i++) {
		if (&mm->vma[i] == skip)
			continue;
		if (vma_overlaps(&mm->vma[i], start, end))
			return true;
	}

	return false;
}

static bool range_overlaps_vma(struct mm_struct *mm, uintptr_t start,
			       uintptr_t end)
{
	for (int i = 0; i < NR_VMA; i++) {
		if (vma_overlaps(&mm->vma[i], start, end))
			return true;
	}

	return false;
}

static uintptr_t find_unmapped_area(struct mm_struct *mm, size_t length)
{
	uintptr_t len;
	uintptr_t low = align_up_page(mm->brk);
	uintptr_t start;

	if (length > TASK_SIZE)
		return 0;

	len = align_up_page(length);
	if (len == 0 || len >= signal_trampoline_start())
		return 0;

	if (low < PAGE_SIZE)
		low = PAGE_SIZE;

	start = (signal_trampoline_start() - len) & PAGE_MASK;

	while (start >= low) {
		if (!signal_trampoline_overlaps(start, start + len) &&
		    !range_overlaps_vma(mm, start, start + len))
			return start;
		if (start < low + PAGE_SIZE)
			break;
		start -= PAGE_SIZE;
	}

	return 0;
}

static void unmap_user_pages(pte_t *pgd, uintptr_t start, uintptr_t end)
{
	for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
		pte_t *pte = walk_page_table(pgd, va, false);

		if (!pte || !pte_present(*pte))
			continue;

		paddr_t pa = pte_to_pa(*pte);
		if (pa >= DRAM_BASE && pa < DRAM_BASE + DRAM_SIZE)
			free_page(__va(pa), 0);

		*pte = 0;
		sfence_vma_addr(va);
	}
}

static int unmap_vma_range(struct mm_struct *mm, struct vm_area_struct *vma,
			   uintptr_t start, uintptr_t end)
{
	if (!vma_overlaps(vma, start, end))
		return 0;

	uintptr_t unmap_start = start > vma->vm_start ? start : vma->vm_start;
	uintptr_t unmap_end = end < vma->vm_end ? end : vma->vm_end;
	uintptr_t old_start = vma->vm_start;
	uintptr_t old_end = vma->vm_end;
	struct vm_area_struct *tail = NULL;

	if (unmap_start != old_start && unmap_end != old_end) {
		tail = vma_alloc_slot(mm);
		if (!tail)
			return -ENOMEM;
	}

	if (unmap_start == old_start && unmap_end == old_end) {
		unmap_user_pages(mm->pgd, unmap_start, unmap_end);
		vma_free_slot(vma);
		return 0;
	}

	if (unmap_start == old_start) {
		unmap_user_pages(mm->pgd, unmap_start, unmap_end);
		vma->vm_start = unmap_end;
		return 0;
	}

	if (unmap_end == old_end) {
		unmap_user_pages(mm->pgd, unmap_start, unmap_end);
		vma->vm_end = unmap_start;
		return 0;
	}

	unmap_user_pages(mm->pgd, unmap_start, unmap_end);
	*tail = *vma;
	tail->vm_start = unmap_end;
	tail->vm_end = old_end;
	vma->vm_end = unmap_start;
	return 0;
}

/*
 * free_user_page_tables - 释放用户页表中的所有页表页和映射的物理页
 * @pgd: 用户 PGD
 *
 * 遍历低 256 个 PGD 项（用户地址空间部分），释放每个有效 PMD 下
 * 的所有 PTE 页和映射的物理页，最后释放 PMD 页和 PGD 页本身。
 * 高 256 项是内核映射的共享引用，不释放。
 */
static void free_user_page_tables(pte_t *pgd)
{
	for (int i = 0; i < 256; i++) {
		if (!pte_present(pgd[i]))
			continue;

		pte_t *pmd = (pte_t *)__va(pte_to_pa(pgd[i]));
		for (int j = 0; j < 512; j++) {
			if (!pte_present(pmd[j]))
				continue;

			pte_t *pt = (pte_t *)__va(pte_to_pa(pmd[j]));
			for (int k = 0; k < 512; k++) {
				if (!pte_present(pt[k]))
					continue;

				vaddr_t va = ((vaddr_t)i << 30) |
					     ((vaddr_t)j << 21) |
					     ((vaddr_t)k << 12);
				paddr_t pa = pte_to_pa(pt[k]);
				if (signal_trampoline_contains(va))
					continue;
				/*
				 * mm_create_user_pgd() currently injects a low
				 * UART MMIO mapping so trap-time printk works
				 * while still running on the user page table.
				 * Only DRAM pages are owned by this mm; MMIO
				 * and other shared mappings must not be
				 * returned to the buddy allocator.
				 */
				if (pa >= DRAM_BASE &&
				    pa < DRAM_BASE + DRAM_SIZE)
					free_page(__va(pa), 0);
			}
			free_page(pt, 0);
		}
		free_page(pmd, 0);
	}
	free_page(pgd, 0);
}

/* ---- 公共接口 ---- */

struct mm_struct *mm_alloc(void)
{
	struct mm_struct *mm = kmalloc(sizeof(struct mm_struct));
	if (!mm)
		return NULL;

	memset(mm, 0, sizeof(struct mm_struct));
	mm->refcount = 1;
	return mm;
}

void mm_get(struct mm_struct *mm)
{
	if (mm)
		mm->refcount++;
}

void mm_put(struct mm_struct *mm)
{
	if (!mm)
		return;

	BUG_ON(mm->refcount == 0);
	mm->refcount--;
	if (mm->refcount == 0)
		mm_destroy(mm);
}

struct mm_struct *dup_mm(struct mm_struct *oldmm)
{
	if (!oldmm)
		return NULL;

	struct mm_struct *newmm = mm_alloc();
	if (!newmm)
		return NULL;

	newmm->pgd = mm_create_user_pgd();
	if (!newmm->pgd) {
		kfree(newmm);
		return NULL;
	}

	newmm->brk = oldmm->brk;
	newmm->code_start = oldmm->code_start;
	newmm->code_end = oldmm->code_end;
	memcpy(newmm->vma, oldmm->vma, sizeof(oldmm->vma));

	for (int i = 0; i < NR_VMA; i++) {
		if (!oldmm->vma[i].used)
			continue;

		uintptr_t start = oldmm->vma[i].vm_start;
		uintptr_t end = oldmm->vma[i].vm_end;

		for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
			pte_t *pte = walk_page_table(oldmm->pgd, va, false);
			if (!pte || !pte_present(*pte))
				continue;

			uintptr_t old_pa = pte_to_pa(*pte);
			void *new_page = get_free_page(0);
			if (!new_page) {
				mm_destroy(newmm);
				return NULL;
			}

			memcpy(new_page, __va(old_pa), PAGE_SIZE);

			pte_t perm = *pte & (PTE_V | PTE_R | PTE_W | PTE_X |
					     PTE_U | PTE_A | PTE_D | PTE_G);
			map_page(newmm->pgd, va, __pa((uintptr_t)new_page),
				 perm);
		}
	}

	return newmm;
}

void mm_destroy(struct mm_struct *mm)
{
	if (!mm)
		return;

	/* 释放用户地址空间中所有映射的物理页和页表页 */
	if (mm->pgd)
		free_user_page_tables(mm->pgd);

	kfree(mm);
}

pte_t *mm_create_user_pgd(void)
{
	/* 1. 分配 PGD 页 */
	pte_t *user_pgd = (pte_t *)get_free_page(0);
	if (!user_pgd)
		return NULL;
	memset(user_pgd, 0, PAGE_SIZE);

	/* 2. 复制内核高地址映射（PGD[256-511]）
	 *    确保 trap 进内核后内核代码/数据仍可访问 */
	pte_t *kern_pgd = current_pgd();
	for (int i = 256; i < 512; i++)
		user_pgd[i] = kern_pgd[i];

	/* 3. 映射 trap 后仍可能访问的低地址 MMIO。 */
	map_page(user_pgd, UART_BASE, UART_BASE, PTE_KERN_RW);
	map_page(user_pgd, VIRTIO_MMIO_BASE, VIRTIO_MMIO_BASE, PTE_KERN_RW);
	if (signal_map_trampoline(user_pgd) < 0) {
		free_user_page_tables(user_pgd);
		return NULL;
	}

	return user_pgd;
}

struct vm_area_struct *find_vma(struct mm_struct *mm, uintptr_t addr)
{
	for (int i = 0; i < NR_VMA; i++) {
		if (mm->vma[i].used && addr >= mm->vma[i].vm_start &&
		    addr < mm->vma[i].vm_end)
			return &mm->vma[i];
	}
	return NULL;
}

/*
 * mm_brk - brk 内部实现
 * @mm:   进程地址空间描述符
 * @addr: 新的 brk 地址，0 表示查询
 *
 * 不允许缩小堆。仅更新 VMA 边界和 brk 指针，不分配物理页。
 * 实际的物理页在缺页时由 do_page_fault() 按需分配（lazy allocation）。
 * 如果当前没有堆 VMA，则创建一个新的。
 *
 * 返回新的 brk 值，失败返回当前 brk 值。
 */
uintptr_t mm_brk(struct mm_struct *mm, uintptr_t addr)
{
	/* 无地址空间 */
	if (!mm)
		return 0;

	/* 查询当前 brk */
	if (addr == 0)
		return mm->brk;

	/* 不允许缩小 */
	if (addr <= mm->brk)
		return mm->brk;

	/* 不允许超过用户地址空间上限 */
	if (addr > TASK_SIZE)
		return mm->brk;

	/* 查找堆 VMA（通过类型标记精确匹配） */
	uintptr_t old_brk = mm->brk;
	struct vm_area_struct *heap_vma = NULL;
	for (int i = 0; i < NR_VMA; i++) {
		if (mm->vma[i].used && mm->vma[i].vm_type == VMA_HEAP) {
			heap_vma = &mm->vma[i];
			break;
		}
	}

	if (!heap_vma) {
		if (range_overlaps_vma(mm, old_brk, addr))
			return old_brk;

		/* 首次扩展：创建堆 VMA */
		heap_vma = vma_alloc_slot(mm);
		if (!heap_vma)
			return old_brk;
		heap_vma->vm_start = old_brk;
		heap_vma->vm_end = addr;
		heap_vma->vm_flags = VM_READ | VM_WRITE;
		heap_vma->vm_type = VMA_HEAP;
		heap_vma->used = true;
	} else {
		if (range_overlaps_other_vma(mm, heap_vma, heap_vma->vm_start,
					     addr))
			return old_brk;

		/* 扩展已有堆 VMA */
		heap_vma->vm_end = addr;
	}

	mm->brk = addr;
	return addr;
}

ssize_t mm_mmap(struct mm_struct *mm, uintptr_t addr, size_t length, int prot,
		int flags)
{
	uintptr_t start;
	uintptr_t end;
	uint32_t vm_flags = 0;
	struct vm_area_struct *vma;

	if (!mm)
		return -ENOMEM;

	if (!(flags & MAP_ANONYMOUS))
		return -ENOSYS;

	if (!(flags & MAP_PRIVATE))
		return -EINVAL;

	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
		return -EINVAL;

	if (flags & MAP_FIXED) {
		if (addr == 0 || (addr & (PAGE_SIZE - 1)))
			return -EINVAL;
		start = addr;
	} else if (addr != 0) {
		start = align_up_page(addr);
	} else {
		start = find_unmapped_area(mm, length);
		if (!start)
			return -ENOMEM;
	}

	if (range_overflows(start, length, &end))
		return -EINVAL;

	if (end > TASK_SIZE || end > USER_STACK_BASE)
		return -EINVAL;

	if (signal_trampoline_overlaps(start, end))
		return -EINVAL;

	if (range_overlaps_vma(mm, start, end))
		return -EINVAL;

	vma = vma_alloc_slot(mm);
	if (!vma)
		return -ENOMEM;

	if (prot & PROT_READ)
		vm_flags |= VM_READ;
	if (prot & PROT_WRITE)
		vm_flags |= VM_READ | VM_WRITE;
	if (prot & PROT_EXEC)
		vm_flags |= VM_EXEC;

	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_flags = vm_flags;
	vma->vm_type = VMA_MMAP;
	vma->used = true;
	return start;
}

int mm_munmap(struct mm_struct *mm, uintptr_t addr, size_t length)
{
	uintptr_t end;

	if (!mm)
		return -ENOMEM;

	if (addr & (PAGE_SIZE - 1))
		return -EINVAL;

	if (range_overflows(addr, length, &end))
		return -EINVAL;

	if (end > TASK_SIZE)
		return -EINVAL;

	for (int i = 0; i < NR_VMA; i++) {
		int ret = unmap_vma_range(mm, &mm->vma[i], addr, end);

		if (ret < 0)
			return ret;
	}

	return 0;
}

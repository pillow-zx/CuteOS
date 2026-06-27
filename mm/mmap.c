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
#include <uapi/mman.h>
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

static int vma_free_slot_count(struct mm_struct *mm)
{
	int count = 0;

	for (int i = 0; i < NR_VMA; i++) {
		if (!mm->vma[i].used)
			count++;
	}

	return count;
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

static bool vma_contains_split_addr(struct vm_area_struct *vma, uintptr_t addr)
{
	return vma->used && addr > vma->vm_start && addr < vma->vm_end;
}

static bool vma_can_merge(const struct vm_area_struct *a,
			  const struct vm_area_struct *b)
{
	if (!a->used || !b->used || a == b)
		return false;
	if (a->vm_flags != b->vm_flags || a->vm_type != b->vm_type)
		return false;
	return a->vm_end == b->vm_start || b->vm_end == a->vm_start;
}

static void vma_merge_all(struct mm_struct *mm)
{
	bool merged;

	do {
		merged = false;
		for (int i = 0; i < NR_VMA && !merged; i++) {
			for (int j = 0; j < NR_VMA; j++) {
				if (i == j || !vma_can_merge(&mm->vma[i],
							     &mm->vma[j]))
					continue;

				if (mm->vma[i].vm_end == mm->vma[j].vm_start)
					mm->vma[i].vm_end = mm->vma[j].vm_end;
				else
					mm->vma[i].vm_start =
						mm->vma[j].vm_start;
				vma_free_slot(&mm->vma[j]);
				merged = true;
				break;
			}
		}
	} while (merged);
}

static int vma_split_at(struct mm_struct *mm, struct vm_area_struct *vma,
			uintptr_t addr)
{
	struct vm_area_struct *tail;

	if (!vma_contains_split_addr(vma, addr))
		return 0;

	tail = vma_alloc_slot(mm);
	if (!tail)
		return -ENOMEM;

	*tail = *vma;
	tail->vm_start = addr;
	vma->vm_end = addr;
	return 0;
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

		if (!pte || !pte_user_page(*pte))
			continue;

		paddr_t pa = pte_to_pa(*pte);
		if (pa >= DRAM_BASE && pa < DRAM_BASE + DRAM_SIZE)
			free_page(__va(pa), 0);

		*pte = 0;
		sfence_vma_addr(va);
	}
}

static int vma_munmap_slots_needed(struct mm_struct *mm, uintptr_t start,
				   uintptr_t end)
{
	int needed = 0;

	for (int i = 0; i < NR_VMA; i++) {
		struct vm_area_struct *vma = &mm->vma[i];
		uintptr_t unmap_start;
		uintptr_t unmap_end;

		if (!vma_overlaps(vma, start, end))
			continue;

		unmap_start = start > vma->vm_start ? start : vma->vm_start;
		unmap_end = end < vma->vm_end ? end : vma->vm_end;

		if (unmap_start > vma->vm_start && unmap_end < vma->vm_end)
			needed++;
	}

	return needed;
}

static int vma_mprotect_slots_needed(struct mm_struct *mm, uintptr_t start,
				     uintptr_t end)
{
	int needed = 0;

	for (int i = 0; i < NR_VMA; i++) {
		struct vm_area_struct *vma = &mm->vma[i];

		if (!vma_overlaps(vma, start, end))
			continue;
		if (vma_contains_split_addr(vma, start))
			needed++;
		if (vma_contains_split_addr(vma, end))
			needed++;
	}

	return needed;
}

static int unmap_vma_range(struct mm_struct *mm, struct vm_area_struct *vma,
			   uintptr_t start, uintptr_t end)
{
	uintptr_t unmap_start;
	uintptr_t unmap_end;

	if (!vma_overlaps(vma, start, end))
		return 0;

	unmap_start = start > vma->vm_start ? start : vma->vm_start;
	unmap_end = end < vma->vm_end ? end : vma->vm_end;

	if (unmap_start == vma->vm_start && unmap_end == vma->vm_end) {
		unmap_user_pages(mm->pgd, unmap_start, unmap_end);
		vma_free_slot(vma);
		return 0;
	}

	if (unmap_start == vma->vm_start) {
		unmap_user_pages(mm->pgd, unmap_start, unmap_end);
		vma->vm_start = unmap_end;
		return 0;
	}

	if (unmap_end == vma->vm_end) {
		unmap_user_pages(mm->pgd, unmap_start, unmap_end);
		vma->vm_end = unmap_start;
		return 0;
	}

	int ret = vma_split_at(mm, vma, unmap_start);
	if (ret < 0)
		return ret;

	struct vm_area_struct *right = find_vma(mm, unmap_start);
	BUG_ON(!right || right == vma);

	unmap_user_pages(mm->pgd, unmap_start, unmap_end);
	right->vm_start = unmap_end;
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
				if (!pte_user_page(pt[k]))
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
	refcount_set(&mm->refcount, 1);
	mutex_init(&mm->mmap_lock);
	return mm;
}

void mm_get(struct mm_struct *mm)
{
	if (mm)
		refcount_inc(&mm->refcount);
}

void mm_put(struct mm_struct *mm)
{
	if (!mm)
		return;

	if (refcount_dec_and_test(&mm->refcount))
		mm_destroy(mm);
}

void mm_lock(struct mm_struct *mm)
{
	BUG_ON(!mm);
	mutex_lock(&mm->mmap_lock);
}

void mm_unlock(struct mm_struct *mm)
{
	BUG_ON(!mm);
	mutex_unlock(&mm->mmap_lock);
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

	mm_lock(oldmm);
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
			if (!pte || !pte_user_page(*pte))
				continue;

			uintptr_t old_pa = pte_to_pa(*pte);
			void *new_page = get_free_page(0);
			if (!new_page) {
				mm_unlock(oldmm);
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
	mm_unlock(oldmm);

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
	uintptr_t ret;
	uintptr_t old_brk;
	struct vm_area_struct *heap_vma = NULL;

	if (!mm)
		return 0;

	mm_lock(mm);
	old_brk = mm->brk;
	ret = old_brk;

	if (addr == 0)
		goto out;

	if (addr <= old_brk)
		goto out;
	if (addr > TASK_SIZE)
		goto out;

	for (int i = 0; i < NR_VMA; i++) {
		if (mm->vma[i].used && mm->vma[i].vm_type == VMA_HEAP) {
			heap_vma = &mm->vma[i];
			break;
		}
	}

	if (!heap_vma) {
		if (range_overlaps_vma(mm, old_brk, addr))
			goto out;

		heap_vma = vma_alloc_slot(mm);
		if (!heap_vma)
			goto out;
		heap_vma->vm_start = old_brk;
		heap_vma->vm_end = addr;
		heap_vma->vm_flags = VM_READ | VM_WRITE;
		heap_vma->vm_type = VMA_HEAP;
		heap_vma->used = true;
	} else {
		if (range_overlaps_other_vma(mm, heap_vma, heap_vma->vm_start,
					     addr))
			goto out;

		heap_vma->vm_end = addr;
	}

	mm->brk = addr;
	vma_merge_all(mm);
	ret = addr;

out:
	mm_unlock(mm);
	return ret;
}

ssize_t mm_mmap(struct mm_struct *mm, uintptr_t addr, size_t length, int prot,
		int flags)
{
	uintptr_t start;
	uintptr_t end;
	uint32_t vm_flags = 0;
	struct vm_area_struct *vma;
	ssize_t ret;

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
	}

	if (prot & PROT_READ)
		vm_flags |= VM_READ;
	if (prot & PROT_WRITE)
		vm_flags |= VM_READ | VM_WRITE;
	if (prot & PROT_EXEC)
		vm_flags |= VM_EXEC;

	mm_lock(mm);

	if (flags & MAP_FIXED) {
		start = addr;
	} else if (addr != 0) {
		start = align_up_page(addr);
	} else {
		start = find_unmapped_area(mm, length);
		if (!start) {
			ret = -ENOMEM;
			goto out;
		}
	}

	if (range_overflows(start, length, &end)) {
		ret = -EINVAL;
		goto out;
	}

	if (end > TASK_SIZE || end > USER_STACK_BASE) {
		ret = -EINVAL;
		goto out;
	}

	if (signal_trampoline_overlaps(start, end)) {
		ret = -EINVAL;
		goto out;
	}

	if (range_overlaps_vma(mm, start, end)) {
		ret = -EINVAL;
		goto out;
	}

	vma = vma_alloc_slot(mm);
	if (!vma) {
		ret = -ENOMEM;
		goto out;
	}

	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_flags = vm_flags;
	vma->vm_type = VMA_MMAP;
	vma->used = true;
	vma_merge_all(mm);
	ret = start;

out:
	mm_unlock(mm);
	return ret;
}

int mm_munmap(struct mm_struct *mm, uintptr_t addr, size_t length)
{
	uintptr_t end;
	int ret = 0;

	if (!mm)
		return -ENOMEM;

	if (addr & (PAGE_SIZE - 1))
		return -EINVAL;

	if (range_overflows(addr, length, &end))
		return -EINVAL;

	if (end > TASK_SIZE)
		return -EINVAL;

	mm_lock(mm);
	if (vma_munmap_slots_needed(mm, addr, end) > vma_free_slot_count(mm)) {
		ret = -ENOMEM;
		goto out;
	}

	for (int i = 0; i < NR_VMA; i++) {
		ret = unmap_vma_range(mm, &mm->vma[i], addr, end);
		if (ret < 0)
			goto out;
	}

out:
	mm_unlock(mm);
	return ret;
}

static bool vma_is_anonymous(const struct vm_area_struct *vma)
{
	return vma->vm_type == VMA_HEAP || vma->vm_type == VMA_STACK ||
	       vma->vm_type == VMA_MMAP;
}

static void madvise_dontneed_range(struct mm_struct *mm, uintptr_t start,
				   uintptr_t end)
{
	for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
		pte_t *pte = walk_page_table(mm->pgd, va, false);

		if (!pte || !pte_user_page(*pte))
			continue;

		paddr_t pa = pte_to_pa(*pte);
		if (pa >= DRAM_BASE && pa < DRAM_BASE + DRAM_SIZE)
			free_page(__va(pa), 0);
		*pte = 0;
		sfence_vma_addr(va);
	}
}

int mm_madvise(struct mm_struct *mm, uintptr_t addr, size_t len, int advice)
{
	uintptr_t end;
	int ret = 0;

	if (!mm)
		return -EINVAL;
	if (addr & (PAGE_SIZE - 1))
		return -EINVAL;
	if (len == 0)
		return 0;
	if (range_overflows(addr, len, &end))
		return -EINVAL;
	if (end > TASK_SIZE)
		return -EINVAL;

	mm_lock(mm);

	for (uintptr_t va = addr; va < end; va += PAGE_SIZE) {
		struct vm_area_struct *vma = find_vma(mm, va);

		if (!vma) {
			ret = -ENOMEM;
			goto out;
		}
		if (advice == MADV_DONTNEED && !vma_is_anonymous(vma)) {
			ret = -EINVAL;
			goto out;
		}
	}

	if (advice == MADV_DONTNEED)
		madvise_dontneed_range(mm, addr, end);

out:
	mm_unlock(mm);
	return ret;
}

/* prot (PROT_READ | PROT_WRITE | PROT_EXEC) → vm_flags */
static uint32_t prot_to_vm_flags(int prot)
{
	uint32_t flags = 0;

	if (prot & PROT_READ)  flags |= VM_READ;
	if (prot & PROT_WRITE) flags |= VM_READ | VM_WRITE;
	if (prot & PROT_EXEC)  flags |= VM_EXEC;
	return flags;
}

/* prot → leaf PTE permission bits (user pages) */
static pte_t prot_to_pte_flags(int prot)
{
	pte_t flags = PTE_V | PTE_U | PTE_A | PTE_D;

	if (prot & PROT_READ)  flags |= PTE_R;
	if (prot & PROT_WRITE) flags |= PTE_R | PTE_W;
	if (prot & PROT_EXEC)  flags |= PTE_X;
	return flags;
}

/*
 * mm_mprotect - 修改地址范围内 VMA 的访问权限并更新页表 PTE。
 *
 * 需要在范围边界分裂 VMA（最多 2 次），更新 vm_flags，然后对所有已映射
 * 的页更新 PTE 权限位并刷新 TLB。
 */
int mm_mprotect(struct mm_struct *mm, uintptr_t addr, size_t len, int prot)
{
	uint32_t new_vm_flags;
	pte_t    new_pte_flags;
	uintptr_t end;
	int ret = 0;

	if (!mm)
		return -EINVAL;
	if (addr & (PAGE_SIZE - 1))
		return -EINVAL;
	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
		return -EINVAL;
	if (len == 0)
		return 0;
	if (len > TASK_SIZE)
		return -EINVAL;

	len = (len + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
	if (range_overflows(addr, len, &end))
		return -EINVAL;
	if (end > TASK_SIZE)
		return -EINVAL;

	new_vm_flags  = prot_to_vm_flags(prot);
	new_pte_flags = prot_to_pte_flags(prot);

	mm_lock(mm);

	/* Entire range must be backed by VMAs. */
	for (uintptr_t va = addr; va < end; va += PAGE_SIZE) {
		if (!find_vma(mm, va)) {
			ret = -ENOMEM;
			goto out;
		}
	}

	if (vma_mprotect_slots_needed(mm, addr, end) >
	    vma_free_slot_count(mm)) {
		ret = -ENOMEM;
		goto out;
	}

	/* Split at addr: any VMA that strictly contains addr gets split. */
	for (int i = 0; i < NR_VMA; i++) {
		ret = vma_split_at(mm, &mm->vma[i], addr);
		if (ret < 0)
			goto out;
	}

	/* Split at end: any VMA that strictly contains end gets split. */
	for (int i = 0; i < NR_VMA; i++) {
		ret = vma_split_at(mm, &mm->vma[i], end);
		if (ret < 0)
			goto out;
	}

	/* Update vm_flags for every VMA fully within [addr, end). */
	for (int i = 0; i < NR_VMA; i++) {
		struct vm_area_struct *v = &mm->vma[i];

		if (!v->used)
			continue;
		if (v->vm_start >= addr && v->vm_end <= end)
			v->vm_flags = new_vm_flags;
	}

	/* Update PTE permissions for already-mapped pages. */
	for (uintptr_t va = addr; va < end; va += PAGE_SIZE) {
		pte_t *pte = walk_page_table(mm->pgd, va, false);

		if (!pte || *pte == 0)
			continue;
		if (prot == PROT_NONE) {
			*pte &= ~PTE_V;
		} else {
			uintptr_t pa = PTE_TO_PA(*pte);
			*pte = PA_TO_PTE(pa) | new_pte_flags;
		}
	}

	sfence_vma_all();
	vma_merge_all(mm);

out:
	mm_unlock(mm);
	return ret;
}

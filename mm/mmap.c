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
 *   mm_create_user_pgd() - 创建用户页表 + 复制内核映射 + 应用注册映射
 *   find_vma()           - 查找包含指定地址的 VMA
 *   mm_brk()             - brk 内部实现（lazy allocation，不缩小）
 */

#include <kernel/mm.h>
#include <kernel/errno.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/slab.h>
#include <kernel/buddy.h>
#include <kernel/task.h>
#include <kernel/user_map.h>
#include <uapi/mman.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/csr.h>

#include "internal.h"

/* ---- 内部辅助函数 ---- */

static uintptr_t find_unmapped_area(struct mm_struct *mm, size_t length)
{
	uintptr_t len;
	uintptr_t low = mm_align_up_page(mm->brk);
	uintptr_t start;

	if (length > TASK_SIZE)
		return 0;

	len = mm_align_up_page(length);
	if (len == 0 || len >= USER_STACK_BASE)
		return 0;

	if (low < PAGE_SIZE)
		low = PAGE_SIZE;

	start = (USER_STACK_BASE - len) & PAGE_MASK;

	while (start >= low) {
		if (!user_map_reserved_overlaps(start, start + len) &&
		    !vma_range_overlaps(mm, start, start + len))
			return start;
		if (start < low + PAGE_SIZE)
			break;
		start -= PAGE_SIZE;
	}

	return 0;
}

static bool mm_owns_page_frame(paddr_t pa)
{
	return pa >= DRAM_BASE && pa < DRAM_BASE + DRAM_SIZE;
}

static void unmap_user_pages(pte_t *pgd, uintptr_t start, uintptr_t end)
{
	for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
		pte_t *pte = arch_pt_walk(pgd, va, false);

		if (!pte || !pte_user_page(*pte))
			continue;

		paddr_t pa = pte_to_pa(*pte);
		if (mm_owns_page_frame(pa))
			free_page(__va(pa), 0);

		*pte = 0;
		arch_tlb_flush_page(va);
	}
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
				if (user_map_reserved_contains(va))
					continue;
				/*
				 * mm_create_user_pgd() currently injects a low
				 * UART MMIO mapping so trap-time printk works
				 * while still running on the user page table.
				 * Only DRAM pages are owned by this mm; MMIO
				 * and other shared mappings must not be
				 * returned to the buddy allocator.
				 */
				if (mm_owns_page_frame(pa))
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
	struct mm_struct *newmm;

	if (!oldmm)
		return NULL;

	newmm = mm_alloc();
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
			pte_t *pte = arch_pt_walk(oldmm->pgd, va, false);
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
			arch_map_page(newmm->pgd, va, __pa((uintptr_t)new_page),
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
	pte_t *user_pgd;
	pte_t *kern_root;
	int ret;

	/* 1. 分配 PGD 页 */
	user_pgd = (pte_t *)get_free_page(0);
	if (!user_pgd)
		return NULL;
	memset(user_pgd, 0, PAGE_SIZE);

	/* 2. 复制内核高地址映射（PGD[256-511]）
	 *    确保 trap 进内核后内核代码/数据仍可访问 */
	kern_root = arch_current_pt();
	for (int i = 256; i < 512; i++)
		user_pgd[i] = kern_root[i];

	/* 3. 应用平台和子系统注册的用户页表特殊映射。 */
	ret = user_map_apply(user_pgd);
	if (ret < 0) {
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
		if (vma_range_overlaps(mm, old_brk, addr))
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
		if (vma_range_overlaps_other(mm, heap_vma, heap_vma->vm_start,
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

	if (length == 0 || length > TASK_SIZE)
		return -EINVAL;

	mm_lock(mm);

	if (flags & MAP_FIXED) {
		start = addr;
	} else if (addr != 0) {
		start = mm_align_up_page(addr);
	} else {
		start = find_unmapped_area(mm, length);
		if (!start) {
			ret = -ENOMEM;
			goto out;
		}
	}

	ret = mm_range_end_page_aligned(start, length, &end);
	if (ret < 0)
		goto out;

	if (end > USER_STACK_BASE) {
		ret = -EINVAL;
		goto out;
	}

	if (user_map_reserved_overlaps(start, end)) {
		ret = -EINVAL;
		goto out;
	}

	if (vma_range_overlaps(mm, start, end)) {
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

	ret = mm_range_end_page_aligned(addr, length, &end);
	if (ret < 0)
		return ret;

	mm_lock(mm);
	if (vma_munmap_slots_needed(mm, addr, end) > vma_free_slot_count(mm)) {
		ret = -ENOMEM;
		goto out;
	}

	for (int i = 0; i < NR_VMA; i++) {
		uintptr_t unmap_start = 0;
		uintptr_t unmap_end = 0;

		ret = vma_unmap_range(mm, &mm->vma[i], addr, end, &unmap_start,
				      &unmap_end);
		if (ret < 0)
			goto out;
		if (ret > 0) {
			unmap_user_pages(mm->pgd, unmap_start, unmap_end);
			ret = 0;
		}
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
		pte_t *pte = arch_pt_walk(mm->pgd, va, false);

		if (!pte || !pte_user_page(*pte))
			continue;

		paddr_t pa = pte_to_pa(*pte);
		if (mm_owns_page_frame(pa))
			free_page(__va(pa), 0);
		*pte = 0;
		arch_tlb_flush_page(va);
	}
}

int mm_madvise(struct mm_struct *mm, uintptr_t addr, size_t len, int advice)
{
	uintptr_t end;
	int ret = 0;
	bool drop_resident = false;

	if (!mm)
		return -EINVAL;
	if (addr & (PAGE_SIZE - 1))
		return -EINVAL;
	if (len == 0)
		return 0;

	switch (advice) {
	case MADV_NORMAL:
	case MADV_RANDOM:
	case MADV_SEQUENTIAL:
	case MADV_WILLNEED:
	case MADV_FREE:
		break;
	case MADV_DONTNEED:
		drop_resident = true;
		break;
	default:
		return -EINVAL;
	}

	ret = mm_range_end_page_aligned(addr, len, &end);
	if (ret < 0)
		return ret;

	mm_lock(mm);

	for (uintptr_t va = addr; va < end; va += PAGE_SIZE) {
		struct vm_area_struct *vma = find_vma(mm, va);

		if (!vma) {
			ret = -ENOMEM;
			goto out;
		}
		if (drop_resident && !vma_is_anonymous(vma)) {
			ret = -EINVAL;
			goto out;
		}
	}

	if (drop_resident)
		madvise_dontneed_range(mm, addr, end);

out:
	mm_unlock(mm);
	return ret;
}

/* prot (PROT_READ | PROT_WRITE | PROT_EXEC) → vm_flags */
static uint32_t prot_to_vm_flags(int prot)
{
	uint32_t flags = 0;

	if (prot & PROT_READ)
		flags |= VM_READ;
	if (prot & PROT_WRITE)
		flags |= VM_READ | VM_WRITE;
	if (prot & PROT_EXEC)
		flags |= VM_EXEC;
	return flags;
}

/* prot → leaf PTE permission bits (user pages) */
static pte_t prot_to_pte_flags(int prot)
{
	pte_t flags = PTE_V | PTE_U | PTE_A | PTE_D;

	if (prot & PROT_READ)
		flags |= PTE_R;
	if (prot & PROT_WRITE)
		flags |= PTE_R | PTE_W;
	if (prot & PROT_EXEC)
		flags |= PTE_X;
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
	pte_t new_pte_flags;
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
	ret = mm_range_end_page_aligned(addr, len, &end);
	if (ret < 0)
		return ret;

	new_vm_flags = prot_to_vm_flags(prot);
	new_pte_flags = prot_to_pte_flags(prot);

	mm_lock(mm);

	if (!vma_range_is_mapped(mm, addr, end)) {
		ret = -ENOMEM;
		goto out;
	}

	if (vma_mprotect_slots_needed(mm, addr, end) >
	    vma_free_slot_count(mm)) {
		ret = -ENOMEM;
		goto out;
	}

	ret = vma_split_range(mm, addr, end);
	if (ret < 0)
		goto out;

	vma_update_flags_range(mm, addr, end, new_vm_flags);

	/* Update PTE permissions for already-mapped pages. */
	for (uintptr_t va = addr; va < end; va += PAGE_SIZE) {
		pte_t *pte = arch_pt_walk(mm->pgd, va, false);

		if (!pte || *pte == 0)
			continue;
		if (prot == PROT_NONE) {
			*pte &= ~PTE_V;
		} else {
			uintptr_t pa = PTE_TO_PA(*pte);
			*pte = PA_TO_PTE(pa) | new_pte_flags;
		}
	}

	arch_tlb_flush_all();
	vma_merge_all(mm);

out:
	mm_unlock(mm);
	return ret;
}

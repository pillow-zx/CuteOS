#ifndef _CUTEOS_MM_INTERNAL_H
#define _CUTEOS_MM_INTERNAL_H

#include <kernel/mm.h>
#include <kernel/bitops.h>
#include <kernel/cleanup.h>
#include <kernel/refcount.h>
#include <kernel/sync.h>
#include <kernel/tools.h>
#include <kernel/types.h>
#include <uapi/mman.h>

#include <kernel/page.h>
#include <kernel/pgtable.h>

struct file;

#define VM_READ	 BIT_U32(0)
#define VM_WRITE BIT_U32(1)
#define VM_EXEC	 BIT_U32(2)

#define VMA_CODE  BIT_U32(0)
#define VMA_HEAP  BIT_U32(1)
#define VMA_STACK BIT_U32(2)
#define VMA_MMAP  BIT_U32(3)

#define NR_VMA 16

#define USER_FAULT_READ	 0
#define USER_FAULT_WRITE 1
#define USER_FAULT_EXEC	 2

struct vm_area_struct {
	uintptr_t vm_start;
	uintptr_t vm_end;
	uint32_t vm_flags;
	uint32_t vm_type;
	struct file *vm_file;
	uint64_t vm_offset;
	bool vm_shared;
	bool used;
};

struct mm_struct {
	refcount_t refcount;
	mutex_t mmap_lock;
	pte_t *pgd;
	uintptr_t brk;
	uintptr_t code_start;
	uintptr_t code_end;
	uint32_t membarrier_registrations;
	struct vm_area_struct vma[NR_VMA];
};

static_assert(NR_VMA > 0, "NR_VMA must stay positive");

static __always_inline __must_check __const uintptr_t
mm_page_align_up(uintptr_t addr)
{
	return ALIGN_UP(addr, PAGE_SIZE);
}

static __always_inline __must_check __pure __nonnull(1) uint64_t
	vma_offset_at(const struct vm_area_struct *vma, uintptr_t va)
{
	return vma->vm_offset + (va - vma->vm_start);
}

static __always_inline __must_check __pure __nonnull(1) uint64_t
	vma_page_index(const struct vm_area_struct *vma, uintptr_t page_addr)
{
	uintptr_t base = vma->vm_start & PAGE_MASK;
	uint64_t file_base = vma->vm_offset & PAGE_MASK;

	return (file_base + (page_addr - base)) / PAGE_SIZE;
}

static __always_inline __nonnull(1) void mm_lock(struct mm_struct *mm)
{
	mutex_lock(&mm->mmap_lock);
}

static __always_inline __nonnull(1) void mm_unlock(struct mm_struct *mm)
{
	mutex_unlock(&mm->mmap_lock);
}

SCOPE_GUARD_DEFINE(mm_guard, struct mm_struct *, mm_lock(_T), mm_unlock(_T))

static __always_inline __must_check __const int vma_capacity(void)
{
	return NR_VMA;
}

static __always_inline __must_check __const bool mm_prot_is_valid(int prot)
{
	return (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) == 0;
}

static __always_inline __must_check __const uint32_t
mm_prot_to_vm_flags(int prot)
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

static __always_inline __must_check __const pgprot_t
mm_prot_to_pte_flags(int prot)
{
	return pgprot_user((prot & PROT_READ) != 0, (prot & PROT_WRITE) != 0,
			   (prot & PROT_EXEC) != 0);
}

static __always_inline __must_check __const pgprot_t
vma_flags_to_pte(uint32_t vm_flags)
{
	return pgprot_user((vm_flags & VM_READ) != 0,
			   (vm_flags & VM_WRITE) != 0,
			   (vm_flags & VM_EXEC) != 0);
}

static __always_inline __must_check __pure __nonnull(1) bool
vma_overlaps(const struct vm_area_struct *vma, uintptr_t start, uintptr_t end)
{
	return vma->used && start < vma->vm_end && end > vma->vm_start;
}

static __always_inline __must_check __pure __nonnull(1) bool
vma_contains_split_addr(const struct vm_area_struct *vma, uintptr_t addr)
{
	return vma->used && addr > vma->vm_start && addr < vma->vm_end;
}

static __always_inline __must_check __pure __nonnull(1) bool
vma_is_anonymous(const struct vm_area_struct *vma)
{
	return !vma->vm_file &&
	       (vma->vm_type == VMA_HEAP || vma->vm_type == VMA_STACK ||
		vma->vm_type == VMA_MMAP);
}

static __always_inline __must_check __pure bool
vma_covers_range(const struct vm_area_struct *vma, uintptr_t start,
		 uintptr_t end)
{
	return vma && vma->used && start >= vma->vm_start && end <= vma->vm_end;
}

struct mm_struct *__must_check mm_alloc(void);
void mm_destroy(struct mm_struct *mm);
pte_t *__must_check mm_create_user_pgd(void);

struct vm_area_struct *__must_check find_vma(struct mm_struct *mm,
					     uintptr_t addr);

int __must_check mm_range_end_page_aligned(uintptr_t start, size_t length,
					   uintptr_t *end);

/*
 * fault_in_user_range - 预缺页用户地址范围
 *
 * 仅执行 VMA/PTE 校验和合法 lazy allocation。失败返回负 errno，
 * 不发送 signal、不退出当前任务。
 */
int __must_check fault_in_user_range(struct mm_struct *mm, uintptr_t addr,
				     size_t size, int access);

struct vm_area_struct *__must_check vma_alloc_slot(struct mm_struct *mm);
void vma_free_slot(struct vm_area_struct *vma);
int vma_free_slot_count(struct mm_struct *mm);

bool vma_range_overlaps(struct mm_struct *mm, uintptr_t start, uintptr_t end);
bool vma_range_overlaps_other(struct mm_struct *mm,
			      const struct vm_area_struct *skip,
			      uintptr_t start, uintptr_t end);
int __must_check vma_split_at(struct mm_struct *mm, struct vm_area_struct *vma,
			      uintptr_t addr);
void vma_merge_all(struct mm_struct *mm);

int vma_munmap_slots_needed(struct mm_struct *mm, uintptr_t start,
			    uintptr_t end);
int vma_mprotect_slots_needed(struct mm_struct *mm, uintptr_t start,
			      uintptr_t end);
bool vma_range_is_mapped(struct mm_struct *mm, uintptr_t start, uintptr_t end);
int __must_check vma_split_range(struct mm_struct *mm, uintptr_t start,
				 uintptr_t end);
void vma_update_flags_range(struct mm_struct *mm, uintptr_t start,
			    uintptr_t end, uint32_t vm_flags);
int __must_check vma_unmap_range(struct mm_struct *mm,
				 struct vm_area_struct *vma, uintptr_t start,
				 uintptr_t end, uintptr_t *unmap_start,
				 uintptr_t *unmap_end);
void mm_unmap_user_pages_locked(struct mm_struct *mm,
				const struct vm_area_struct *vma,
				uintptr_t start, uintptr_t end);
int __must_check mm_move_user_pages_locked(struct mm_struct *mm,
					   uintptr_t old_start,
					   uintptr_t new_start, size_t len);

#endif /* _CUTEOS_MM_INTERNAL_H */

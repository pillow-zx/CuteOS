#ifndef _CUTEOS_MM_INTERNAL_H
#define _CUTEOS_MM_INTERNAL_H

#include <kernel/mm.h>
#include <kernel/refcount.h>
#include <kernel/sync.h>
#include <kernel/types.h>
#include <asm/pte.h>

struct file;

#define VM_READ	 0x01
#define VM_WRITE 0x02
#define VM_EXEC	 0x04

#define VMA_CODE  0x01
#define VMA_HEAP  0x02
#define VMA_STACK 0x04
#define VMA_MMAP  0x08

#define NR_VMA 16

struct vm_area_struct {
	uintptr_t vm_start;
	uintptr_t vm_end;
	uint32_t vm_flags;
	uint32_t vm_type;
	struct file *vm_file;
	uint64_t vm_pgoff;
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

struct mm_struct *__must_check mm_alloc(void);
void mm_destroy(struct mm_struct *mm);
pte_t *__must_check mm_create_user_pgd(void);
void mm_lock(struct mm_struct *mm);
void mm_unlock(struct mm_struct *mm);
struct vm_area_struct *__must_check find_vma(struct mm_struct *mm,
					     uintptr_t addr);

uintptr_t mm_align_up_page(uintptr_t addr);
int __must_check mm_range_end_page_aligned(uintptr_t start, size_t length,
					   uintptr_t *end);

struct vm_area_struct *__must_check vma_alloc_slot(struct mm_struct *mm);
void vma_free_slot(struct vm_area_struct *vma);
int vma_free_slot_count(struct mm_struct *mm);
int mm_vma_capacity(void);

bool vma_overlaps(const struct vm_area_struct *vma, uintptr_t start,
		  uintptr_t end);
bool vma_range_overlaps(struct mm_struct *mm, uintptr_t start, uintptr_t end);
bool vma_range_overlaps_other(struct mm_struct *mm,
			      const struct vm_area_struct *skip,
			      uintptr_t start, uintptr_t end);
bool vma_contains_split_addr(const struct vm_area_struct *vma, uintptr_t addr);
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

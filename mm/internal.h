#ifndef _CUTEOS_MM_INTERNAL_H
#define _CUTEOS_MM_INTERNAL_H

#include <kernel/mm.h>
#include <kernel/types.h>

uintptr_t mm_align_up_page(uintptr_t addr);
int __must_check mm_range_end_page_aligned(uintptr_t start, size_t length,
					   uintptr_t *end);

struct vm_area_struct *__must_check vma_alloc_slot(struct mm_struct *mm);
void vma_free_slot(struct vm_area_struct *vma);
int vma_free_slot_count(struct mm_struct *mm);

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

#endif /* _CUTEOS_MM_INTERNAL_H */

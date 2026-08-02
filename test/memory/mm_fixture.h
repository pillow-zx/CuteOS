#ifndef _CUTEOS_TEST_MEMORY_MM_FIXTURE_H
#define _CUTEOS_TEST_MEMORY_MM_FIXTURE_H

#include <kernel/errno.h>
#include <kernel/buddy.h>
#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/test.h>
#include <kernel/vmalloc.h>
#include <kernel/page.h>
#include <kernel/pgtable.h>

#include "../../mm/internal.h"
#include "../io/memory_fixture.h"
#include "../ktest.h"

#define MM_TEST_BASE		0x00400000UL
#define MM_TEST_GAP		0x00100000UL
#define MM_TEST_VMALLOC_SIZE	(128UL << 20)
#define MM_TEST_VMALLOC_L0_SIZE (512UL * PAGE_SIZE)

struct vma_snapshot {
	bool found;
	uintptr_t start;
	uintptr_t end;
	uint32_t flags;
	uint32_t type;
};

static inline struct mm_struct *mm_test_alloc(void)
{
	return mm_create_user();
}

static inline int mm_test_map_shared_file(struct mm_struct *mm,
						 struct file *file, uintptr_t start,
						 uintptr_t end, int prot,
						 uint64_t file_offset)
{
	struct vm_area_struct *vma;

	if (!mm || !file || start >= end || (start & (PAGE_SIZE - 1)) ||
	    (end & (PAGE_SIZE - 1)) ||
	    (file_offset & (PAGE_SIZE - 1)))
		return -EINVAL;
	mm_lock(mm);
	if (vma_range_overlaps(mm, start, end)) {
		mm_unlock(mm);
		return -EINVAL;
	}
	vma = vma_alloc_slot(mm);
	if (!vma) {
		mm_unlock(mm);
		return -ENOMEM;
	}
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_flags = mm_prot_to_vm_flags(prot);
	vma->vm_type = VMA_MMAP;
	vma->vm_file = file;
	vma->vm_offset = file_offset;
	vma->vm_shared = true;
	vma->used = true;
	file_get(file);
	mm_unlock(mm);
	return 0;
}

static inline int mm_test_read_raw_file_page(struct ktest_memory_file *file,
						      uint32_t index, uint8_t *buf)
{
	if (!file || !buf)
		return -EINVAL;
	return ktest_memory_file_read_block(file, index, buf);
}

static inline int mm_test_count_vmas(struct mm_struct *mm)
{
	int count = 0;

	with_guard(mm_guard, mm)
	{
		for (int i = 0; i < vma_capacity(); i++) {
			if (mm->vma[i].used)
				count++;
		}
	}

	return count;
}

static inline int mm_test_count_type(struct mm_struct *mm, uint32_t type)
{
	int count = 0;

	with_guard(mm_guard, mm)
	{
		for (int i = 0; i < vma_capacity(); i++) {
			if (mm->vma[i].used && mm->vma[i].vm_type == type)
				count++;
		}
	}

	return count;
}

static inline struct vma_snapshot mm_test_snapshot(struct mm_struct *mm,
						   uintptr_t addr)
{
	struct vma_snapshot snapshot = {0};
	struct vm_area_struct *vma;

	with_guard(mm_guard, mm)
	{
		vma = find_vma(mm, addr);
		if (!vma)
			break;

		snapshot.found = true;
		snapshot.start = vma->vm_start;
		snapshot.end = vma->vm_end;
		snapshot.flags = vma->vm_flags;
		snapshot.type = vma->vm_type;
	}

	return snapshot;
}

#endif

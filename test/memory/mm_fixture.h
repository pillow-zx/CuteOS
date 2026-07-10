#ifndef _CUTEOS_TEST_MEMORY_MM_FIXTURE_H
#define _CUTEOS_TEST_MEMORY_MM_FIXTURE_H

#include <kernel/errno.h>
#include <kernel/buddy.h>
#include <kernel/blkdev.h>
#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/test.h>
#include <kernel/vmalloc.h>
#include <kernel/vfs.h>
#include <kernel/page.h>
#include <kernel/pgtable.h>

#include "../../fs/ext2/ext2.h"
#include "../../mm/internal.h"
#include "../ktest.h"

#define MM_TEST_BASE		0x00400000UL
#define MM_TEST_GAP		0x00100000UL
#define MM_TEST_FILE		"/mm_exec_text_test"
#define MM_MSYNC_TEST_FILE	"/mm_msync_shared_test"
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

static inline int mm_test_read_raw_file_page(struct file *file, uint32_t index,
					      uint8_t *buf)
{
	struct block_device *bdev;
	uint32_t pblock = 0;
	int ret;

	if (!file || !file->f_inode || !buf)
		return -EINVAL;

	ret = ext2_bmap(file->f_inode, index, false, &pblock);
	if (ret < 0)
		return ret;
	if (!pblock)
		return -ENOENT;

	bdev = lookup_block_device(file->f_inode->i_sb->s_dev);
	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->read_sectors)
		return -ENXIO;

	return bdev->bd_ops->read_sectors(bdev, buf, pblock * BLOCK_SECTORS,
					  BLOCK_SECTORS);
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

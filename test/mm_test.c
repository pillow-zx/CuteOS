#include <kernel/errno.h>
#include <kernel/buddy.h>
#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/test.h>
#include <kernel/vfs.h>
#include <asm/page.h>
#include <asm/pte.h>

#include "../mm/internal.h"

#define MM_TEST_BASE 0x00400000UL
#define MM_TEST_GAP  0x00100000UL
#define MM_TEST_FILE "/mm_exec_text_test"

struct vma_snapshot {
	bool found;
	uintptr_t start;
	uintptr_t end;
	uint32_t flags;
	uint32_t type;
};

static struct mm_struct *mm_test_alloc(void)
{
	return mm_create_user();
}

void test_map_page_first_table_oom_rolls_back(void)
{
	TEST_BEGIN("map_page: first table OOM rolls back");
	{
		pte_t *root = get_free_page(0);
		void *page = get_free_page(0);
		size_t free_before;
		int ret;

		TEST_ASSERT_NOT_NULL(root);
		TEST_ASSERT_NOT_NULL(page);
		memset(root, 0, PAGE_SIZE);

		free_before = buddy_free_pages();
		arch_pt_test_fail_alloc_after(0);
		ret = map_page(root, MM_TEST_BASE, __pa((uintptr_t)page),
			       PTE_USER_RW);
		arch_pt_test_clear_alloc_failure();

		TEST_ASSERT_EQ(ret, -ENOMEM);
		TEST_ASSERT_NULL(arch_pt_walk(root, MM_TEST_BASE, false));
		TEST_ASSERT_EQ(buddy_free_pages(), free_before);

		free_page(page, 0);
		free_page(root, 0);
	}
	TEST_END("map_page: first table OOM rolls back");
	return;
fail:
	arch_pt_test_clear_alloc_failure();
	TEST_FAIL("map_page: first table OOM rolls back", "see above");
}

void test_map_page_second_table_oom_rolls_back(void)
{
	TEST_BEGIN("map_page: second table OOM rolls back");
	{
		pte_t *root = get_free_page(0);
		void *page = get_free_page(0);
		size_t free_before;
		int ret;

		TEST_ASSERT_NOT_NULL(root);
		TEST_ASSERT_NOT_NULL(page);
		memset(root, 0, PAGE_SIZE);

		free_before = buddy_free_pages();
		arch_pt_test_fail_alloc_after(1);
		ret = map_page(root, MM_TEST_BASE, __pa((uintptr_t)page),
			       PTE_USER_RW);
		arch_pt_test_clear_alloc_failure();

		TEST_ASSERT_EQ(ret, -ENOMEM);
		TEST_ASSERT_NULL(arch_pt_walk(root, MM_TEST_BASE, false));
		TEST_ASSERT_EQ(buddy_free_pages(), free_before);

		free_page(page, 0);
		free_page(root, 0);
	}
	TEST_END("map_page: second table OOM rolls back");
	return;
fail:
	arch_pt_test_clear_alloc_failure();
	TEST_FAIL("map_page: second table OOM rolls back", "see above");
}

static int mm_test_count_vmas(struct mm_struct *mm)
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

static int mm_test_count_type(struct mm_struct *mm, uint32_t type)
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

static struct vma_snapshot mm_test_snapshot(struct mm_struct *mm,
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

void test_mm_vma_merge_adjacent(void)
{
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE;

	TEST_BEGIN("mm: adjacent mmap merge");
	{
		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);

		TEST_ASSERT_EQ(mm_mmap(mm, base, PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)base);
		TEST_ASSERT_EQ(mm_mmap(mm, base + PAGE_SIZE, PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)(base + PAGE_SIZE));

		struct vma_snapshot vma = mm_test_snapshot(mm, base);
		TEST_ASSERT(vma.found);
		TEST_ASSERT_EQ(mm_test_count_type(mm, VMA_MMAP), 1);
		TEST_ASSERT_EQ(vma.start, base);
		TEST_ASSERT_EQ(vma.end, base + 2 * PAGE_SIZE);
		TEST_ASSERT_EQ(vma.flags, (uint32_t)(VM_READ | VM_WRITE));
	}
	TEST_END("mm: adjacent mmap merge");
	goto cleanup;
fail:
	TEST_FAIL("mm: adjacent mmap merge", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
}

void test_mm_vma_munmap_middle_split(void)
{
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + MM_TEST_GAP;

	TEST_BEGIN("mm: munmap middle split");
	{
		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);

		TEST_ASSERT_EQ(mm_mmap(mm, base, 3 * PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)base);
		TEST_ASSERT_EQ(mm_munmap(mm, base + PAGE_SIZE, PAGE_SIZE), 0);

		struct vma_snapshot head = mm_test_snapshot(mm, base);
		struct vma_snapshot hole =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		struct vma_snapshot tail =
			mm_test_snapshot(mm, base + 2 * PAGE_SIZE);

		TEST_ASSERT_EQ(mm_test_count_type(mm, VMA_MMAP), 2);
		TEST_ASSERT(head.found);
		TEST_ASSERT(!hole.found);
		TEST_ASSERT(tail.found);
		TEST_ASSERT_EQ(head.start, base);
		TEST_ASSERT_EQ(head.end, base + PAGE_SIZE);
		TEST_ASSERT_EQ(tail.start, base + 2 * PAGE_SIZE);
		TEST_ASSERT_EQ(tail.end, base + 3 * PAGE_SIZE);
	}
	TEST_END("mm: munmap middle split");
	goto cleanup;
fail:
	TEST_FAIL("mm: munmap middle split", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
}

void test_mm_vma_munmap_head_tail_trim(void)
{
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + 2 * MM_TEST_GAP;

	TEST_BEGIN("mm: munmap head and tail trim");
	{
		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);

		TEST_ASSERT_EQ(mm_mmap(mm, base, 3 * PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)base);
		TEST_ASSERT_EQ(mm_munmap(mm, base, PAGE_SIZE), 0);

		struct vma_snapshot after_head =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(after_head.found);
		TEST_ASSERT_EQ(mm_test_count_type(mm, VMA_MMAP), 1);
		TEST_ASSERT_EQ(after_head.start, base + PAGE_SIZE);
		TEST_ASSERT_EQ(after_head.end, base + 3 * PAGE_SIZE);

		TEST_ASSERT_EQ(mm_munmap(mm, base + 2 * PAGE_SIZE, PAGE_SIZE),
			       0);

		struct vma_snapshot after_tail =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(after_tail.found);
		TEST_ASSERT_EQ(mm_test_count_type(mm, VMA_MMAP), 1);
		TEST_ASSERT_EQ(after_tail.start, base + PAGE_SIZE);
		TEST_ASSERT_EQ(after_tail.end, base + 2 * PAGE_SIZE);
	}
	TEST_END("mm: munmap head and tail trim");
	goto cleanup;
fail:
	TEST_FAIL("mm: munmap head and tail trim", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
}

void test_mm_vma_split_enospc_preserves_layout(void)
{
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + 3 * MM_TEST_GAP;

	TEST_BEGIN("mm: split ENOMEM preserves layout");
	{
		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);

		TEST_ASSERT_EQ(mm_mmap(mm, base, 3 * PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)base);

		for (int i = 0; i < vma_capacity() - 1; i++) {
			uintptr_t addr = base + MM_TEST_GAP +
					 (uintptr_t)i * 2 * PAGE_SIZE;
			TEST_ASSERT_EQ(mm_mmap(mm, addr, PAGE_SIZE, PROT_READ,
					       MAP_PRIVATE | MAP_ANONYMOUS |
						       MAP_FIXED),
				       (ssize_t)addr);
		}

		TEST_ASSERT_EQ(mm_test_count_vmas(mm), vma_capacity());
		TEST_ASSERT_EQ(mm_munmap(mm, base + PAGE_SIZE, PAGE_SIZE),
			       -ENOMEM);

		struct vma_snapshot vma =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(vma.found);
		TEST_ASSERT_EQ(mm_test_count_vmas(mm), vma_capacity());
		TEST_ASSERT_EQ(vma.start, base);
		TEST_ASSERT_EQ(vma.end, base + 3 * PAGE_SIZE);
	}
	TEST_END("mm: split ENOMEM preserves layout");
	goto cleanup;
fail:
	TEST_FAIL("mm: split ENOMEM preserves layout", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
}

void test_mm_vma_munmap_full_table_edge_trim(void)
{
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + 4 * MM_TEST_GAP;

	TEST_BEGIN("mm: full VMA table edge trim");
	{
		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);

		TEST_ASSERT_EQ(mm_mmap(mm, base, 3 * PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)base);

		for (int i = 0; i < vma_capacity() - 1; i++) {
			uintptr_t addr = base + MM_TEST_GAP +
					 (uintptr_t)i * 2 * PAGE_SIZE;
			TEST_ASSERT_EQ(mm_mmap(mm, addr, PAGE_SIZE, PROT_READ,
					       MAP_PRIVATE | MAP_ANONYMOUS |
						       MAP_FIXED),
				       (ssize_t)addr);
		}

		TEST_ASSERT_EQ(mm_test_count_vmas(mm), vma_capacity());
		TEST_ASSERT_EQ(mm_munmap(mm, base, PAGE_SIZE), 0);

		struct vma_snapshot after_head =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(after_head.found);
		TEST_ASSERT_EQ(mm_test_count_vmas(mm), vma_capacity());
		TEST_ASSERT_EQ(after_head.start, base + PAGE_SIZE);
		TEST_ASSERT_EQ(after_head.end, base + 3 * PAGE_SIZE);

		TEST_ASSERT_EQ(mm_munmap(mm, base + 2 * PAGE_SIZE, PAGE_SIZE),
			       0);

		struct vma_snapshot after_tail =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(after_tail.found);
		TEST_ASSERT_EQ(mm_test_count_vmas(mm), vma_capacity());
		TEST_ASSERT_EQ(after_tail.start, base + PAGE_SIZE);
		TEST_ASSERT_EQ(after_tail.end, base + 2 * PAGE_SIZE);
	}
	TEST_END("mm: full VMA table edge trim");
	goto cleanup;
fail:
	TEST_FAIL("mm: full VMA table edge trim", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
}

void test_mm_dup_split_vmas(void)
{
	struct mm_struct *mm = NULL;
	struct mm_struct *copy = NULL;
	uintptr_t base = MM_TEST_BASE + 5 * MM_TEST_GAP;

	TEST_BEGIN("mm: dup split VMA layout");
	{
		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);

		TEST_ASSERT_EQ(mm_mmap(mm, base, 3 * PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)base);
		TEST_ASSERT_EQ(mm_munmap(mm, base + PAGE_SIZE, PAGE_SIZE), 0);

		copy = dup_mm(mm);
		TEST_ASSERT_NOT_NULL(copy);

		struct vma_snapshot head = mm_test_snapshot(copy, base);
		struct vma_snapshot hole =
			mm_test_snapshot(copy, base + PAGE_SIZE);
		struct vma_snapshot tail =
			mm_test_snapshot(copy, base + 2 * PAGE_SIZE);

		TEST_ASSERT_EQ(mm_test_count_type(copy, VMA_MMAP), 2);
		TEST_ASSERT(head.found);
		TEST_ASSERT(!hole.found);
		TEST_ASSERT(tail.found);
		TEST_ASSERT_EQ(head.start, base);
		TEST_ASSERT_EQ(head.end, base + PAGE_SIZE);
		TEST_ASSERT_EQ(tail.start, base + 2 * PAGE_SIZE);
		TEST_ASSERT_EQ(tail.end, base + 3 * PAGE_SIZE);
	}
	TEST_END("mm: dup split VMA layout");
	goto cleanup;
fail:
	TEST_FAIL("mm: dup split VMA layout", "see above");
cleanup:
	if (copy)
		mm_destroy(copy);
	if (mm)
		mm_destroy(mm);
}

void test_mm_vma_mprotect_split_merge(void)
{
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + 6 * MM_TEST_GAP;

	TEST_BEGIN("mm: mprotect split and merge");
	{
		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);

		TEST_ASSERT_EQ(mm_mmap(mm, base, 3 * PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)base);
		TEST_ASSERT_EQ(
			mm_mprotect(mm, base + PAGE_SIZE, PAGE_SIZE, PROT_READ),
			0);

		struct vma_snapshot head = mm_test_snapshot(mm, base);
		struct vma_snapshot middle =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		struct vma_snapshot tail =
			mm_test_snapshot(mm, base + 2 * PAGE_SIZE);

		TEST_ASSERT_EQ(mm_test_count_type(mm, VMA_MMAP), 3);
		TEST_ASSERT(head.found);
		TEST_ASSERT(middle.found);
		TEST_ASSERT(tail.found);
		TEST_ASSERT_EQ(head.start, base);
		TEST_ASSERT_EQ(head.end, base + PAGE_SIZE);
		TEST_ASSERT_EQ(head.flags, (uint32_t)(VM_READ | VM_WRITE));
		TEST_ASSERT_EQ(middle.start, base + PAGE_SIZE);
		TEST_ASSERT_EQ(middle.end, base + 2 * PAGE_SIZE);
		TEST_ASSERT_EQ(middle.flags, (uint32_t)VM_READ);
		TEST_ASSERT_EQ(tail.start, base + 2 * PAGE_SIZE);
		TEST_ASSERT_EQ(tail.end, base + 3 * PAGE_SIZE);
		TEST_ASSERT_EQ(tail.flags, (uint32_t)(VM_READ | VM_WRITE));

		TEST_ASSERT_EQ(mm_mprotect(mm, base + PAGE_SIZE, PAGE_SIZE,
					   PROT_READ | PROT_WRITE),
			       0);

		struct vma_snapshot merged = mm_test_snapshot(mm, base);
		TEST_ASSERT_EQ(mm_test_count_type(mm, VMA_MMAP), 1);
		TEST_ASSERT(merged.found);
		TEST_ASSERT_EQ(merged.start, base);
		TEST_ASSERT_EQ(merged.end, base + 3 * PAGE_SIZE);
		TEST_ASSERT_EQ(merged.flags, (uint32_t)(VM_READ | VM_WRITE));
	}
	TEST_END("mm: mprotect split and merge");
	goto cleanup;
fail:
	TEST_FAIL("mm: mprotect split and merge", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
}

void test_mm_vma_mprotect_enospc_preserves_layout(void)
{
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + 7 * MM_TEST_GAP;

	TEST_BEGIN("mm: mprotect ENOMEM preserves layout");
	{
		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);

		TEST_ASSERT_EQ(mm_mmap(mm, base, 3 * PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)base);

		for (int i = 0; i < vma_capacity() - 1; i++) {
			uintptr_t addr = base + MM_TEST_GAP +
					 (uintptr_t)i * 2 * PAGE_SIZE;
			TEST_ASSERT_EQ(mm_mmap(mm, addr, PAGE_SIZE, PROT_READ,
					       MAP_PRIVATE | MAP_ANONYMOUS |
						       MAP_FIXED),
				       (ssize_t)addr);
		}

		TEST_ASSERT_EQ(mm_test_count_vmas(mm), vma_capacity());
		TEST_ASSERT_EQ(
			mm_mprotect(mm, base + PAGE_SIZE, PAGE_SIZE, PROT_READ),
			-ENOMEM);

		struct vma_snapshot vma =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(vma.found);
		TEST_ASSERT_EQ(mm_test_count_vmas(mm), vma_capacity());
		TEST_ASSERT_EQ(vma.start, base);
		TEST_ASSERT_EQ(vma.end, base + 3 * PAGE_SIZE);
		TEST_ASSERT_EQ(vma.flags, (uint32_t)(VM_READ | VM_WRITE));
	}
	TEST_END("mm: mprotect ENOMEM preserves layout");
	goto cleanup;
fail:
	TEST_FAIL("mm: mprotect ENOMEM preserves layout", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
}

void test_mm_madvise_supported_hints_are_noop(void)
{
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + 8 * MM_TEST_GAP;

	TEST_BEGIN("mm: madvise supported hints return 0");
	{
		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);

		TEST_ASSERT_EQ(mm_mmap(mm, base, PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)base);
		TEST_ASSERT_EQ(mm_madvise(mm, base, PAGE_SIZE, MADV_NORMAL), 0);
		TEST_ASSERT_EQ(mm_madvise(mm, base, PAGE_SIZE, MADV_RANDOM), 0);
		TEST_ASSERT_EQ(mm_madvise(mm, base, PAGE_SIZE, MADV_SEQUENTIAL),
			       0);
		TEST_ASSERT_EQ(mm_madvise(mm, base, PAGE_SIZE, MADV_WILLNEED),
			       0);
		TEST_ASSERT_EQ(mm_madvise(mm, base, PAGE_SIZE, MADV_FREE), 0);
		TEST_ASSERT_EQ(mm_madvise(mm, base, PAGE_SIZE, 0x7fff),
			       -EINVAL);
	}
	TEST_END("mm: madvise supported hints return 0");
	goto cleanup;
fail:
	TEST_FAIL("mm: madvise supported hints return 0", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
}

void test_mm_move_user_pages_preserves_resident_page(void)
{
	struct mm_struct *mm = NULL;
	uintptr_t src = MM_TEST_BASE + 9 * MM_TEST_GAP;
	uintptr_t dst = MM_TEST_BASE + 10 * MM_TEST_GAP;

	TEST_BEGIN("mm: move resident user page");
	{
		pte_t *src_pte;
		pte_t *dst_pte;
		paddr_t src_pa;
		paddr_t dst_pa;
		uint8_t *data;

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);

		TEST_ASSERT_EQ(mm_mmap(mm, src, PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)src);
		TEST_ASSERT_EQ(mm_mmap(mm, dst, PAGE_SIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED),
			       (ssize_t)dst);
		TEST_ASSERT_EQ(fault_in_user_range(mm, src, PAGE_SIZE,
						   USER_FAULT_WRITE),
			       0);

		src_pte = arch_pt_walk(mm->pgd, src, false);
		TEST_ASSERT(src_pte && pte_user_page(*src_pte));
		src_pa = pte_to_pa(*src_pte);
		data = (uint8_t *)__va(src_pa);
		data[0] = 0x5a;
		data[PAGE_SIZE - 1] = 0xa5;

		mm_lock(mm);
		TEST_ASSERT_EQ(
			mm_move_user_pages_locked(mm, src, dst, PAGE_SIZE), 0);
		mm_unlock(mm);

		src_pte = arch_pt_walk(mm->pgd, src, false);
		dst_pte = arch_pt_walk(mm->pgd, dst, false);
		TEST_ASSERT(!src_pte || !pte_user_page(*src_pte));
		TEST_ASSERT(dst_pte && pte_user_page(*dst_pte));
		dst_pa = pte_to_pa(*dst_pte);
		TEST_ASSERT_EQ(dst_pa, src_pa);
		data = (uint8_t *)__va(dst_pa);
		TEST_ASSERT_EQ(data[0], 0x5a);
		TEST_ASSERT_EQ(data[PAGE_SIZE - 1], 0xa5);
	}
	TEST_END("mm: move resident user page");
	goto cleanup;
fail:
	TEST_FAIL("mm: move resident user page", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
}

void test_mm_exec_file_segment_faults_lazily(void)
{
	static char page_data[PAGE_SIZE];
	struct mm_struct *mm = NULL;
	struct file *file = NULL;
	uintptr_t base = MM_TEST_BASE + 20 * MM_TEST_GAP;
	bool resident = true;
	int fd = -1;

	(void)vfs_unlink_at_path(NULL, MM_TEST_FILE, 0);

	TEST_BEGIN("mm: exec file segment faults lazily");
	{
		for (size_t i = 0; i < sizeof(page_data); i++)
			page_data[i] = (char)(0x40 + (i & 0x3f));

		fd = vfs_open(MM_TEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
		TEST_ASSERT(fd >= 0);
		file = fd_get(fd);
		TEST_ASSERT_NOT_NULL(file);
		TEST_ASSERT_EQ(vfs_write(file, page_data, sizeof(page_data)),
			       (ssize_t)sizeof(page_data));

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_exec_map_file_segment(
				       mm, file, base, base + PAGE_SIZE,
				       PROT_READ | PROT_EXEC, 0),
			       0);
		TEST_ASSERT_EQ(mm_user_page_resident(mm, base, &resident), 0);
		TEST_ASSERT(!resident);

		TEST_ASSERT_EQ(
			fault_in_user_range(mm, base, 1, USER_FAULT_EXEC), 0);
		TEST_ASSERT_EQ(mm_user_page_resident(mm, base, &resident), 0);
		TEST_ASSERT(resident);
	}
	TEST_END("mm: exec file segment faults lazily");
	goto cleanup;
fail:
	TEST_FAIL("mm: exec file segment faults lazily", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
	if (file)
		file_put(file);
	if (fd >= 0)
		fd_close(fd);
	(void)vfs_unlink_at_path(NULL, MM_TEST_FILE, 0);
}

void test_mm_exec_file_segment_zero_fills_tail(void)
{
	static char file_data[2 * PAGE_SIZE];
	struct mm_struct *mm = NULL;
	struct file *file = NULL;
	uintptr_t base = MM_TEST_BASE + 21 * MM_TEST_GAP;
	uintptr_t end = base + PAGE_SIZE + 100;
	pte_t *pte;
	uint8_t *mapped;
	int fd = -1;

	(void)vfs_unlink_at_path(NULL, MM_TEST_FILE, 0);

	TEST_BEGIN("mm: exec file segment zero fills tail");
	{
		for (size_t i = 0; i < sizeof(file_data); i++)
			file_data[i] = (char)(0x20 + (i & 0x5f));

		fd = vfs_open(MM_TEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
		TEST_ASSERT(fd >= 0);
		file = fd_get(fd);
		TEST_ASSERT_NOT_NULL(file);
		TEST_ASSERT_EQ(vfs_write(file, file_data, sizeof(file_data)),
			       (ssize_t)sizeof(file_data));

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_exec_map_file_segment(mm, file, base, end,
							PROT_READ | PROT_EXEC,
							0),
			       0);
		TEST_ASSERT_EQ(fault_in_user_range(mm, base + PAGE_SIZE, 1,
						   USER_FAULT_EXEC),
			       0);

		pte = arch_pt_walk(mm->pgd, base + PAGE_SIZE, false);
		TEST_ASSERT(pte && pte_user_page(*pte));
		mapped = (uint8_t *)__va(pte_to_pa(*pte));
		TEST_ASSERT_EQ(mapped[99], (uint8_t)file_data[PAGE_SIZE + 99]);
		TEST_ASSERT_EQ(mapped[100], (uint8_t)0);
		TEST_ASSERT_EQ(mapped[PAGE_SIZE - 1], (uint8_t)0);
	}
	TEST_END("mm: exec file segment zero fills tail");
	goto cleanup;
fail:
	TEST_FAIL("mm: exec file segment zero fills tail", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
	if (file)
		file_put(file);
	if (fd >= 0)
		fd_close(fd);
	(void)vfs_unlink_at_path(NULL, MM_TEST_FILE, 0);
}

void test_mm_exec_file_segment_split_keeps_offset(void)
{
	static char file_data[3 * PAGE_SIZE];
	struct mm_struct *mm = NULL;
	struct file *file = NULL;
	uintptr_t base = MM_TEST_BASE + 22 * MM_TEST_GAP;
	uintptr_t start = base + 128;
	pte_t *pte;
	uint8_t *mapped;
	int fd = -1;

	(void)vfs_unlink_at_path(NULL, MM_TEST_FILE, 0);

	TEST_BEGIN("mm: exec file segment split keeps offset");
	{
		for (size_t i = 0; i < sizeof(file_data); i++)
			file_data[i] = (char)(0x10 + (i / PAGE_SIZE) * 0x20 +
					      (i & 0x1f));

		fd = vfs_open(MM_TEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
		TEST_ASSERT(fd >= 0);
		file = fd_get(fd);
		TEST_ASSERT_NOT_NULL(file);
		TEST_ASSERT_EQ(vfs_write(file, file_data, sizeof(file_data)),
			       (ssize_t)sizeof(file_data));

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_exec_map_file_segment(
				       mm, file, start, start + 2 * PAGE_SIZE,
				       PROT_READ | PROT_EXEC, 128),
			       0);
		TEST_ASSERT_EQ(
			mm_mprotect(mm, base + PAGE_SIZE, PAGE_SIZE, PROT_READ),
			0);
		TEST_ASSERT_EQ(fault_in_user_range(mm, base + PAGE_SIZE, 1,
						   USER_FAULT_READ),
			       0);

		pte = arch_pt_walk(mm->pgd, base + PAGE_SIZE, false);
		TEST_ASSERT(pte && pte_user_page(*pte));
		mapped = (uint8_t *)__va(pte_to_pa(*pte));
		TEST_ASSERT_EQ(mapped[0], (uint8_t)file_data[PAGE_SIZE]);
		TEST_ASSERT_EQ(mapped[127],
			       (uint8_t)file_data[PAGE_SIZE + 127]);
	}
	TEST_END("mm: exec file segment split keeps offset");
	goto cleanup;
fail:
	TEST_FAIL("mm: exec file segment split keeps offset", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
	if (file)
		file_put(file);
	if (fd >= 0)
		fd_close(fd);
	(void)vfs_unlink_at_path(NULL, MM_TEST_FILE, 0);
}

void test_mm_exec_file_segment_trim_keeps_offset(void)
{
	static char file_data[3 * PAGE_SIZE];
	struct mm_struct *mm = NULL;
	struct file *file = NULL;
	uintptr_t base = MM_TEST_BASE + 23 * MM_TEST_GAP;
	uintptr_t start = base + 128;
	pte_t *pte;
	uint8_t *mapped;
	int fd = -1;

	(void)vfs_unlink_at_path(NULL, MM_TEST_FILE, 0);

	TEST_BEGIN("mm: exec file segment trim keeps offset");
	{
		for (size_t i = 0; i < sizeof(file_data); i++)
			file_data[i] = (char)(0x30 + (i / PAGE_SIZE) * 0x10 +
					      (i & 0x0f));

		fd = vfs_open(MM_TEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
		TEST_ASSERT(fd >= 0);
		file = fd_get(fd);
		TEST_ASSERT_NOT_NULL(file);
		TEST_ASSERT_EQ(vfs_write(file, file_data, sizeof(file_data)),
			       (ssize_t)sizeof(file_data));

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_exec_map_file_segment(
				       mm, file, start, start + 2 * PAGE_SIZE,
				       PROT_READ | PROT_EXEC, 128),
			       0);
		TEST_ASSERT_EQ(mm_munmap(mm, base, PAGE_SIZE), 0);
		TEST_ASSERT_EQ(fault_in_user_range(mm, base + PAGE_SIZE, 1,
						   USER_FAULT_EXEC),
			       0);

		pte = arch_pt_walk(mm->pgd, base + PAGE_SIZE, false);
		TEST_ASSERT(pte && pte_user_page(*pte));
		mapped = (uint8_t *)__va(pte_to_pa(*pte));
		TEST_ASSERT_EQ(mapped[0], (uint8_t)file_data[PAGE_SIZE]);
		TEST_ASSERT_EQ(mapped[63], (uint8_t)file_data[PAGE_SIZE + 63]);
	}
	TEST_END("mm: exec file segment trim keeps offset");
	goto cleanup;
fail:
	TEST_FAIL("mm: exec file segment trim keeps offset", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
	if (file)
		file_put(file);
	if (fd >= 0)
		fd_close(fd);
	(void)vfs_unlink_at_path(NULL, MM_TEST_FILE, 0);
}

void test_mm_exec_file_segment_merge_requires_contiguous_offset(void)
{
	static char file_data[3 * PAGE_SIZE];
	struct mm_struct *mm = NULL;
	struct file *file = NULL;
	uintptr_t base = MM_TEST_BASE + 24 * MM_TEST_GAP;
	int fd = -1;

	(void)vfs_unlink_at_path(NULL, MM_TEST_FILE, 0);

	TEST_BEGIN("mm: exec file segment merge requires contiguous offset");
	{
		memset(file_data, 0x5a, sizeof(file_data));
		fd = vfs_open(MM_TEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
		TEST_ASSERT(fd >= 0);
		file = fd_get(fd);
		TEST_ASSERT_NOT_NULL(file);
		TEST_ASSERT_EQ(vfs_write(file, file_data, sizeof(file_data)),
			       (ssize_t)sizeof(file_data));

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_exec_map_file_segment(
				       mm, file, base + 128, base + PAGE_SIZE,
				       PROT_READ | PROT_EXEC, 128),
			       0);
		TEST_ASSERT_EQ(mm_exec_map_file_segment(
				       mm, file, base + PAGE_SIZE,
				       base + PAGE_SIZE + 128,
				       PROT_READ | PROT_EXEC, PAGE_SIZE),
			       0);
		vma_merge_all(mm);
		TEST_ASSERT_EQ(mm_test_count_type(mm, VMA_CODE), 1);

		TEST_ASSERT_EQ(
			mm_exec_map_file_segment(mm, file, base + 2 * PAGE_SIZE,
						 base + 3 * PAGE_SIZE,
						 PROT_READ | PROT_EXEC, 0),
			0);
		vma_merge_all(mm);
		TEST_ASSERT_EQ(mm_test_count_type(mm, VMA_CODE), 2);
	}
	TEST_END("mm: exec file segment merge requires contiguous offset");
	goto cleanup;
fail:
	TEST_FAIL("mm: exec file segment merge requires contiguous offset",
		  "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
	if (file)
		file_put(file);
	if (fd >= 0)
		fd_close(fd);
	(void)vfs_unlink_at_path(NULL, MM_TEST_FILE, 0);
}

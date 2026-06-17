#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/test.h>
#include <asm/page.h>

#define MM_TEST_BASE 0x00400000UL
#define MM_TEST_GAP  0x00100000UL

struct vma_snapshot {
	bool found;
	uintptr_t start;
	uintptr_t end;
	uint32_t flags;
	uint32_t type;
};

static struct mm_struct *mm_test_alloc(void)
{
	struct mm_struct *mm = mm_alloc();
	if (!mm)
		return NULL;

	mm->pgd = mm_create_user_pgd();
	if (!mm->pgd) {
		mm_destroy(mm);
		return NULL;
	}

	return mm;
}

static int mm_test_count_vmas(struct mm_struct *mm)
{
	int count = 0;

	mm_lock(mm);
	for (int i = 0; i < NR_VMA; i++) {
		if (mm->vma[i].used)
			count++;
	}
	mm_unlock(mm);

	return count;
}

static int mm_test_count_type(struct mm_struct *mm, uint32_t type)
{
	int count = 0;

	mm_lock(mm);
	for (int i = 0; i < NR_VMA; i++) {
		if (mm->vma[i].used && mm->vma[i].vm_type == type)
			count++;
	}
	mm_unlock(mm);

	return count;
}

static struct vma_snapshot mm_test_snapshot(struct mm_struct *mm, uintptr_t addr)
{
	struct vma_snapshot snapshot = { 0 };

	mm_lock(mm);
	struct vm_area_struct *vma = find_vma(mm, addr);
	if (vma) {
		snapshot.found = true;
		snapshot.start = vma->vm_start;
		snapshot.end = vma->vm_end;
		snapshot.flags = vma->vm_flags;
		snapshot.type = vma->vm_type;
	}
	mm_unlock(mm);

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

		TEST_ASSERT_EQ(mm_mmap(mm, base, PAGE_SIZE, PROT_READ | PROT_WRITE,
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
		struct vma_snapshot hole = mm_test_snapshot(mm, base + PAGE_SIZE);
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

		TEST_ASSERT_EQ(mm_munmap(mm, base + 2 * PAGE_SIZE, PAGE_SIZE), 0);

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

		for (int i = 0; i < NR_VMA - 1; i++) {
			uintptr_t addr = base + MM_TEST_GAP +
					 (uintptr_t)i * 2 * PAGE_SIZE;
			TEST_ASSERT_EQ(mm_mmap(mm, addr, PAGE_SIZE, PROT_READ,
					       MAP_PRIVATE | MAP_ANONYMOUS |
						       MAP_FIXED),
				       (ssize_t)addr);
		}

		TEST_ASSERT_EQ(mm_test_count_vmas(mm), NR_VMA);
		TEST_ASSERT_EQ(mm_munmap(mm, base + PAGE_SIZE, PAGE_SIZE),
			       -ENOMEM);

		struct vma_snapshot vma = mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(vma.found);
		TEST_ASSERT_EQ(mm_test_count_vmas(mm), NR_VMA);
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

		for (int i = 0; i < NR_VMA - 1; i++) {
			uintptr_t addr = base + MM_TEST_GAP +
					 (uintptr_t)i * 2 * PAGE_SIZE;
			TEST_ASSERT_EQ(mm_mmap(mm, addr, PAGE_SIZE, PROT_READ,
					       MAP_PRIVATE | MAP_ANONYMOUS |
						       MAP_FIXED),
				       (ssize_t)addr);
		}

		TEST_ASSERT_EQ(mm_test_count_vmas(mm), NR_VMA);
		TEST_ASSERT_EQ(mm_munmap(mm, base, PAGE_SIZE), 0);

		struct vma_snapshot after_head =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(after_head.found);
		TEST_ASSERT_EQ(mm_test_count_vmas(mm), NR_VMA);
		TEST_ASSERT_EQ(after_head.start, base + PAGE_SIZE);
		TEST_ASSERT_EQ(after_head.end, base + 3 * PAGE_SIZE);

		TEST_ASSERT_EQ(mm_munmap(mm, base + 2 * PAGE_SIZE, PAGE_SIZE),
			       0);

		struct vma_snapshot after_tail =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(after_tail.found);
		TEST_ASSERT_EQ(mm_test_count_vmas(mm), NR_VMA);
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
		struct vma_snapshot hole = mm_test_snapshot(copy, base + PAGE_SIZE);
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

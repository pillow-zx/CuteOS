#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/test.h>
#include <asm/page.h>
#include <asm/pte.h>

#include "../mm/internal.h"

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
	return mm_create_user();
}

static int mm_test_count_vmas(struct mm_struct *mm)
{
	int count = 0;

	mm_lock(mm);
		for (int i = 0; i < mm_vma_capacity(); i++) {
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
	for (int i = 0; i < mm_vma_capacity(); i++) {
		if (mm->vma[i].used && mm->vma[i].vm_type == type)
			count++;
	}
	mm_unlock(mm);

	return count;
}

static struct vma_snapshot mm_test_snapshot(struct mm_struct *mm,
					    uintptr_t addr)
{
	struct vma_snapshot snapshot = {0};

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

		for (int i = 0; i < mm_vma_capacity() - 1; i++) {
			uintptr_t addr = base + MM_TEST_GAP +
					 (uintptr_t)i * 2 * PAGE_SIZE;
			TEST_ASSERT_EQ(mm_mmap(mm, addr, PAGE_SIZE, PROT_READ,
					       MAP_PRIVATE | MAP_ANONYMOUS |
						       MAP_FIXED),
				       (ssize_t)addr);
		}

		TEST_ASSERT_EQ(mm_test_count_vmas(mm), mm_vma_capacity());
		TEST_ASSERT_EQ(mm_munmap(mm, base + PAGE_SIZE, PAGE_SIZE),
			       -ENOMEM);

		struct vma_snapshot vma =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(vma.found);
		TEST_ASSERT_EQ(mm_test_count_vmas(mm), mm_vma_capacity());
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

		for (int i = 0; i < mm_vma_capacity() - 1; i++) {
			uintptr_t addr = base + MM_TEST_GAP +
					 (uintptr_t)i * 2 * PAGE_SIZE;
			TEST_ASSERT_EQ(mm_mmap(mm, addr, PAGE_SIZE, PROT_READ,
					       MAP_PRIVATE | MAP_ANONYMOUS |
						       MAP_FIXED),
				       (ssize_t)addr);
		}

		TEST_ASSERT_EQ(mm_test_count_vmas(mm), mm_vma_capacity());
		TEST_ASSERT_EQ(mm_munmap(mm, base, PAGE_SIZE), 0);

		struct vma_snapshot after_head =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(after_head.found);
		TEST_ASSERT_EQ(mm_test_count_vmas(mm), mm_vma_capacity());
		TEST_ASSERT_EQ(after_head.start, base + PAGE_SIZE);
		TEST_ASSERT_EQ(after_head.end, base + 3 * PAGE_SIZE);

		TEST_ASSERT_EQ(mm_munmap(mm, base + 2 * PAGE_SIZE, PAGE_SIZE),
			       0);

		struct vma_snapshot after_tail =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(after_tail.found);
		TEST_ASSERT_EQ(mm_test_count_vmas(mm), mm_vma_capacity());
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

		for (int i = 0; i < mm_vma_capacity() - 1; i++) {
			uintptr_t addr = base + MM_TEST_GAP +
					 (uintptr_t)i * 2 * PAGE_SIZE;
			TEST_ASSERT_EQ(mm_mmap(mm, addr, PAGE_SIZE, PROT_READ,
					       MAP_PRIVATE | MAP_ANONYMOUS |
						       MAP_FIXED),
				       (ssize_t)addr);
		}

		TEST_ASSERT_EQ(mm_test_count_vmas(mm), mm_vma_capacity());
		TEST_ASSERT_EQ(
			mm_mprotect(mm, base + PAGE_SIZE, PAGE_SIZE, PROT_READ),
			-ENOMEM);

		struct vma_snapshot vma =
			mm_test_snapshot(mm, base + PAGE_SIZE);
		TEST_ASSERT(vma.found);
		TEST_ASSERT_EQ(mm_test_count_vmas(mm), mm_vma_capacity());
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
		TEST_ASSERT_EQ(mm_move_user_pages_locked(mm, src, dst,
							 PAGE_SIZE),
			       0);
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

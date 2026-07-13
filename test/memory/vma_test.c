#include "mm_fixture.h"

int test_mm_vma_merge_adjacent(void)
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

	return __test_ret;
}

int test_mm_vma_munmap_middle_split(void)
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

	return __test_ret;
}

int test_mm_vma_munmap_head_tail_trim(void)
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

	return __test_ret;
}

int test_mm_vma_split_enospc_preserves_layout(void)
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

	return __test_ret;
}

int test_mm_vma_munmap_full_table_edge_trim(void)
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

	return __test_ret;
}

int test_mm_dup_split_vmas(void)
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

	return __test_ret;
}

int test_mm_vma_mprotect_split_merge(void)
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

	return __test_ret;
}

int test_mm_vma_mprotect_enospc_preserves_layout(void)
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

	return __test_ret;
}

int test_mm_madvise_supported_hints_are_noop(void)
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

	return __test_ret;
}

int test_mm_move_user_pages_preserves_resident_page(void)
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

		src_pte = pagetable_walk(mm->pgd, src, false);
		TEST_ASSERT(src_pte && pte_is_user_page(*src_pte));
		src_pa = pte_phys_addr(*src_pte);
		data = (uint8_t *)__va(src_pa);
		data[0] = 0x5a;
		data[PAGE_SIZE - 1] = 0xa5;

		mm_lock(mm);
		TEST_ASSERT_EQ(
			mm_move_user_pages_locked(mm, src, dst, PAGE_SIZE), 0);
		mm_unlock(mm);

		src_pte = pagetable_walk(mm->pgd, src, false);
		dst_pte = pagetable_walk(mm->pgd, dst, false);
		TEST_ASSERT(!src_pte || !pte_is_user_page(*src_pte));
		TEST_ASSERT(dst_pte && pte_is_user_page(*dst_pte));
		dst_pa = pte_phys_addr(*dst_pte);
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

	return __test_ret;
}

int test_mm_msync_shared_mapping_writes_back(void)
{
	static uint8_t initial[PAGE_SIZE];
	static uint8_t raw[PAGE_SIZE];
	struct mm_struct *mm = NULL;
	struct file *file = NULL;
	uintptr_t base = MM_TEST_BASE + 11 * MM_TEST_GAP;
	int fd = -1;

	(void)vfs_unlink_at_path(NULL, MM_MSYNC_TEST_FILE, 0);

	TEST_BEGIN("mm: msync shared mapping writes back");
	{
		pte_t *pte;
		uint8_t *mapped;

		for (size_t i = 0; i < sizeof(initial); i++)
			initial[i] = (uint8_t)(0x31 + (i & 0x1f));

		fd = vfs_open(MM_MSYNC_TEST_FILE, O_RDWR | O_CREAT | O_TRUNC,
			      0644);
		TEST_ASSERT(fd >= 0);
		file = fd_get(fd);
		TEST_ASSERT_NOT_NULL(file);
		TEST_ASSERT_EQ(
			vfs_write(file, (const char *)initial, sizeof(initial)),
			(ssize_t)sizeof(initial));
		TEST_ASSERT_EQ(vfs_sync_file(file), 0);

		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(mm_test_read_raw_file_page(file, 0, raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, initial, sizeof(raw)), 0);

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_mmap_file(mm, base, PAGE_SIZE,
					    PROT_READ | PROT_WRITE,
					    MAP_SHARED | MAP_FIXED, fd, 0),
			       (ssize_t)base);
		TEST_ASSERT_EQ(fault_in_user_range(mm, base, PAGE_SIZE,
						   USER_FAULT_WRITE),
			       0);

		pte = pagetable_walk(mm->pgd, base, false);
		TEST_ASSERT(pte && pte_is_user_page(*pte));
		mapped = (uint8_t *)__va(pte_phys_addr(*pte));
		mapped[17] = 0x6a;
		mapped[PAGE_SIZE - 19] = 0x7b;

		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(mm_test_read_raw_file_page(file, 0, raw), 0);
		TEST_ASSERT_NE(raw[17], 0x6a);
		TEST_ASSERT_NE(raw[PAGE_SIZE - 19], 0x7b);

		TEST_ASSERT_EQ(mm_msync(mm, base, PAGE_SIZE, MS_SYNC), 0);

		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(mm_test_read_raw_file_page(file, 0, raw), 0);
		TEST_ASSERT_EQ(raw[17], 0x6a);
		TEST_ASSERT_EQ(raw[PAGE_SIZE - 19], 0x7b);
	}
	TEST_END("mm: msync shared mapping writes back");
	goto cleanup;
fail:
	TEST_FAIL("mm: msync shared mapping writes back", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
	if (file)
		file_put(file);
	if (fd >= 0)
		fd_close(fd);
	(void)vfs_unlink_at_path(NULL, MM_MSYNC_TEST_FILE, 0);

	return __test_ret;
}

int test_mm_sparse_shared_mapping_writes_back(void)
{
	static uint8_t raw[PAGE_SIZE];
	struct mm_struct *mm = NULL;
	struct file *file = NULL;
	uintptr_t base = MM_TEST_BASE + 12 * MM_TEST_GAP;
	uint8_t marker = 0x7d;
	int fd = -1;

	(void)vfs_unlink_at_path(NULL, "/mm_sparse_shared_test", 0);

	TEST_BEGIN("mm: sparse shared mapping writes back");
	{
		pte_t *pte;
		uint8_t *mapped;

		fd = vfs_open("/mm_sparse_shared_test",
			      O_RDWR | O_CREAT | O_TRUNC, 0644);
		TEST_ASSERT(fd >= 0);
		file = fd_get(fd);
		TEST_ASSERT_NOT_NULL(file);
		file->f_pos = PAGE_SIZE;
		TEST_ASSERT_EQ(vfs_write(file, (const char *)&marker, 1), 1);
		TEST_ASSERT_EQ(vfs_sync_file(file), 0);
		TEST_ASSERT_EQ(mm_test_read_raw_file_page(file, 0, raw), -ENOENT);

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_mmap_file(mm, base, PAGE_SIZE,
					    PROT_READ | PROT_WRITE,
					    MAP_SHARED | MAP_FIXED, fd, 0),
				       (ssize_t)base);
		TEST_ASSERT_EQ(fault_in_user_range(mm, base, PAGE_SIZE,
						   USER_FAULT_READ), 0);
		pte = pagetable_walk(mm->pgd, base, false);
		TEST_ASSERT(pte && pte_is_user_page(*pte));
		mapped = (uint8_t *)__va(pte_phys_addr(*pte));
		for (size_t i = 0; i < PAGE_SIZE; i++)
			TEST_ASSERT_EQ(mapped[i], 0);

		mapped[37] = marker;
		TEST_ASSERT_EQ(mm_msync(mm, base, PAGE_SIZE, MS_SYNC), 0);
		TEST_ASSERT_EQ(mm_test_read_raw_file_page(file, 0, raw), 0);
		TEST_ASSERT_EQ(raw[37], marker);
	}
	TEST_END("mm: sparse shared mapping writes back");
	goto cleanup;
fail:
	TEST_FAIL("mm: sparse shared mapping writes back", "see above");
cleanup:
	if (mm)
		mm_destroy(mm);
	if (file)
		file_put(file);
	if (fd >= 0)
		fd_close(fd);
	(void)vfs_unlink_at_path(NULL, "/mm_sparse_shared_test", 0);

	return __test_ret;
}

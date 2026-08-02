#include "mm_fixture.h"

static int seed_file_pages(struct ktest_memory_file *file, const char *data,
				   uint32_t count)
{
	for (uint32_t i = 0; i < count; i++) {
		if (ktest_memory_file_seed_block(file, i,
						data + (size_t)i * PAGE_SIZE) < 0)
			return -EIO;
	}
	return 0;
}

int test_mm_exec_file_segment_faults_lazily(void)
{
	static char page_data[PAGE_SIZE];
	struct ktest_memory_file fixture = {0};
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + 20 * MM_TEST_GAP;
	bool resident = true;

	TEST_BEGIN("mm: exec file segment faults lazily");
	{
		for (size_t i = 0; i < sizeof(page_data); i++)
			page_data[i] = (char)(0x40 + (i & 0x3f));
		TEST_ASSERT_EQ(ktest_memory_file_init(&fixture,
						     KTEST_MEMORY_DEV(40), 0),
				       0);
		TEST_ASSERT_EQ(seed_file_pages(&fixture, page_data, 1), 0);

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_map_file_segment(mm, &fixture.file, base,
							   base + PAGE_SIZE,
							   PROT_READ | PROT_EXEC, 0),
				       0);
		TEST_ASSERT_EQ(mm_user_page_resident(mm, base, &resident), 0);
		TEST_ASSERT(!resident);

		TEST_ASSERT_EQ(fault_in_user_range(mm, base, 1, USER_FAULT_EXEC),
				       0);
		TEST_ASSERT_EQ(mm_user_page_resident(mm, base, &resident), 0);
		TEST_ASSERT(resident);
	}
	TEST_END("mm: exec file segment faults lazily");
	if (mm)
		mm_destroy(mm);
	ktest_memory_file_destroy(&fixture);
	return __test_ret;
fail:
	TEST_FAIL("mm: exec file segment faults lazily", "see above");
	if (mm)
		mm_destroy(mm);
	ktest_memory_file_destroy(&fixture);
	return __test_ret;
}

int test_mm_exec_file_segment_zero_fills_tail(void)
{
	static char file_data[2 * PAGE_SIZE];
	struct ktest_memory_file fixture = {0};
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + 21 * MM_TEST_GAP;
	uintptr_t end = base + PAGE_SIZE + 100;
	pte_t *pte;
	uint8_t *mapped;

	TEST_BEGIN("mm: exec file segment zero fills tail");
	{
		for (size_t i = 0; i < sizeof(file_data); i++)
			file_data[i] = (char)(0x20 + (i & 0x5f));
		TEST_ASSERT_EQ(ktest_memory_file_init(&fixture,
						     KTEST_MEMORY_DEV(41), 0),
				       0);
		TEST_ASSERT_EQ(seed_file_pages(&fixture, file_data, 2), 0);

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_map_file_segment(mm, &fixture.file, base, end,
							   PROT_READ | PROT_EXEC, 0),
				       0);
		TEST_ASSERT_EQ(fault_in_user_range(mm, base + PAGE_SIZE, 1,
							   USER_FAULT_EXEC),
				       0);

		pte = pagetable_walk(mm->pgd, base + PAGE_SIZE, false);
		TEST_ASSERT(pte && pte_is_user_page(*pte));
		mapped = (uint8_t *)__va(pte_phys_addr(*pte));
		TEST_ASSERT_EQ(mapped[99], (uint8_t)file_data[PAGE_SIZE + 99]);
		TEST_ASSERT_EQ(mapped[100], (uint8_t)0);
		TEST_ASSERT_EQ(mapped[PAGE_SIZE - 1], (uint8_t)0);
	}
	TEST_END("mm: exec file segment zero fills tail");
	if (mm)
		mm_destroy(mm);
	ktest_memory_file_destroy(&fixture);
	return __test_ret;
fail:
	TEST_FAIL("mm: exec file segment zero fills tail", "see above");
	if (mm)
		mm_destroy(mm);
	ktest_memory_file_destroy(&fixture);
	return __test_ret;
}

int test_mm_exec_file_segment_split_keeps_offset(void)
{
	static char file_data[3 * PAGE_SIZE];
	struct ktest_memory_file fixture = {0};
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + 22 * MM_TEST_GAP;
	uintptr_t start = base + 128;
	pte_t *pte;
	uint8_t *mapped;

	TEST_BEGIN("mm: exec file segment split keeps offset");
	{
		for (size_t i = 0; i < sizeof(file_data); i++)
			file_data[i] = (char)(0x10 + (i / PAGE_SIZE) * 0x20 +
					      (i & 0x1f));
		TEST_ASSERT_EQ(ktest_memory_file_init(&fixture,
						     KTEST_MEMORY_DEV(42), 0),
				       0);
		TEST_ASSERT_EQ(seed_file_pages(&fixture, file_data, 3), 0);

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_map_file_segment(mm, &fixture.file, start,
							   start + 2 * PAGE_SIZE,
							   PROT_READ | PROT_EXEC, 128),
				       0);
		TEST_ASSERT_EQ(mm_mprotect(mm, base + PAGE_SIZE, PAGE_SIZE,
							   PROT_READ), 0);
		TEST_ASSERT_EQ(fault_in_user_range(mm, base + PAGE_SIZE, 1,
							   USER_FAULT_READ),
				       0);

		pte = pagetable_walk(mm->pgd, base + PAGE_SIZE, false);
		TEST_ASSERT(pte && pte_is_user_page(*pte));
		mapped = (uint8_t *)__va(pte_phys_addr(*pte));
		TEST_ASSERT_EQ(mapped[0], (uint8_t)file_data[PAGE_SIZE]);
		TEST_ASSERT_EQ(mapped[127],
				       (uint8_t)file_data[PAGE_SIZE + 127]);
	}
	TEST_END("mm: exec file segment split keeps offset");
	if (mm)
		mm_destroy(mm);
	ktest_memory_file_destroy(&fixture);
	return __test_ret;
fail:
	TEST_FAIL("mm: exec file segment split keeps offset", "see above");
	if (mm)
		mm_destroy(mm);
	ktest_memory_file_destroy(&fixture);
	return __test_ret;
}

int test_mm_exec_file_segment_trim_keeps_offset(void)
{
	static char file_data[3 * PAGE_SIZE];
	struct ktest_memory_file fixture = {0};
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + 23 * MM_TEST_GAP;
	uintptr_t start = base + 128;
	pte_t *pte;
	uint8_t *mapped;

	TEST_BEGIN("mm: exec file segment trim keeps offset");
	{
		for (size_t i = 0; i < sizeof(file_data); i++)
			file_data[i] = (char)(0x30 + (i / PAGE_SIZE) * 0x10 +
					      (i & 0x0f));
		TEST_ASSERT_EQ(ktest_memory_file_init(&fixture,
						     KTEST_MEMORY_DEV(43), 0),
				       0);
		TEST_ASSERT_EQ(seed_file_pages(&fixture, file_data, 3), 0);

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_map_file_segment(mm, &fixture.file, start,
							   start + 2 * PAGE_SIZE,
							   PROT_READ | PROT_EXEC, 128),
				       0);
		TEST_ASSERT_EQ(mm_munmap(mm, base, PAGE_SIZE), 0);
		TEST_ASSERT_EQ(fault_in_user_range(mm, base + PAGE_SIZE, 1,
							   USER_FAULT_EXEC),
				       0);

		pte = pagetable_walk(mm->pgd, base + PAGE_SIZE, false);
		TEST_ASSERT(pte && pte_is_user_page(*pte));
		mapped = (uint8_t *)__va(pte_phys_addr(*pte));
		TEST_ASSERT_EQ(mapped[0], (uint8_t)file_data[PAGE_SIZE]);
		TEST_ASSERT_EQ(mapped[63], (uint8_t)file_data[PAGE_SIZE + 63]);
	}
	TEST_END("mm: exec file segment trim keeps offset");
	if (mm)
		mm_destroy(mm);
	ktest_memory_file_destroy(&fixture);
	return __test_ret;
fail:
	TEST_FAIL("mm: exec file segment trim keeps offset", "see above");
	if (mm)
		mm_destroy(mm);
	ktest_memory_file_destroy(&fixture);
	return __test_ret;
}

int test_mm_exec_file_segment_merge_requires_contiguous_offset(void)
{
	static char file_data[3 * PAGE_SIZE];
	struct ktest_memory_file fixture = {0};
	struct mm_struct *mm = NULL;
	uintptr_t base = MM_TEST_BASE + 24 * MM_TEST_GAP;

	TEST_BEGIN("mm: exec file segment merge requires contiguous offset");
	{
		memset(file_data, 0x5a, sizeof(file_data));
		TEST_ASSERT_EQ(ktest_memory_file_init(&fixture,
						     KTEST_MEMORY_DEV(44), 0),
				       0);
		TEST_ASSERT_EQ(seed_file_pages(&fixture, file_data, 3), 0);

		mm = mm_test_alloc();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_map_file_segment(mm, &fixture.file,
							   base + 128, base + PAGE_SIZE,
							   PROT_READ | PROT_EXEC, 128),
				       0);
		TEST_ASSERT_EQ(mm_map_file_segment(mm, &fixture.file,
							   base + PAGE_SIZE,
							   base + PAGE_SIZE + 128,
							   PROT_READ | PROT_EXEC, PAGE_SIZE),
				       0);
		vma_merge_all(mm);
		TEST_ASSERT_EQ(mm_test_count_type(mm, VMA_CODE), 1);

		TEST_ASSERT_EQ(mm_map_file_segment(mm, &fixture.file,
							   base + 2 * PAGE_SIZE,
							   base + 3 * PAGE_SIZE,
							   PROT_READ | PROT_EXEC, 0),
				       0);
		vma_merge_all(mm);
		TEST_ASSERT_EQ(mm_test_count_type(mm, VMA_CODE), 2);
	}
	TEST_END("mm: exec file segment merge requires contiguous offset");
	if (mm)
		mm_destroy(mm);
	ktest_memory_file_destroy(&fixture);
	return __test_ret;
fail:
	TEST_FAIL("mm: exec file segment merge requires contiguous offset",
		  "see above");
	if (mm)
		mm_destroy(mm);
	ktest_memory_file_destroy(&fixture);
	return __test_ret;
}

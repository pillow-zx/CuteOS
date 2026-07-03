#include <kernel/slab.h>
#include <kernel/buddy.h>
#include <kernel/test.h>
#include <kernel/string.h>

#include "ktest.h"

void test_slab_basic(void)
{
	TEST_BEGIN("slab: basic alloc/free");
	{
#define SLAB_NR_CACHES 8
		const size_t sizes[SLAB_NR_CACHES] = {16,  32,	64,   128,
						      256, 512, 1024, 2048};
		void *ptrs[SLAB_NR_CACHES];

		/* Phase 1: 分配各大小并写入模式 */
		for (int i = 0; i < SLAB_NR_CACHES; i++) {
			ptrs[i] = kmalloc(sizes[i]);
			TEST_ASSERT_NOT_NULL(ptrs[i]);
			memset(ptrs[i], 0xAA, sizes[i]);
		}

		/* Phase 2: 全部释放 */
		for (int i = 0; i < SLAB_NR_CACHES; i++)
			kfree(ptrs[i]);

		/* Phase 3: 再次分配（应从 free_list 取回） */
		for (int i = 0; i < SLAB_NR_CACHES; i++) {
			ptrs[i] = kmalloc(sizes[i]);
			TEST_ASSERT_NOT_NULL(ptrs[i]);
		}

		/* Phase 4: 再次释放 */
		for (int i = 0; i < SLAB_NR_CACHES; i++)
			kfree(ptrs[i]);
#undef SLAB_NR_CACHES
	}
	TEST_END("slab: basic alloc/free");
	return;
fail:
	TEST_FAIL("slab: basic alloc/free", "see above");
}
void test_slab_cross_cache(void)
{
	TEST_BEGIN("slab: cross-cache sizes");
	{
		/* 非对齐大小，kmalloc 应向上取整 */
		size_t odd_sizes[] = {1,   7,	15,  17,   33,	65,
				      100, 200, 500, 1000, 1500};
		int n = sizeof(odd_sizes) / sizeof(odd_sizes[0]);
		void *ptrs[16];

		TEST_ASSERT(n <= 16);

		for (int i = 0; i < n; i++) {
			ptrs[i] = kmalloc(odd_sizes[i]);
			TEST_ASSERT_NOT_NULL(ptrs[i]);
			memset(ptrs[i], 0xEE, odd_sizes[i]);
		}

		for (int i = 0; i < n; i++)
			kfree(ptrs[i]);
	}
	TEST_END("slab: cross-cache sizes");
	return;
fail:
	TEST_FAIL("slab: cross-cache sizes", "see above");
}

void test_slab_stress(void)
{
	TEST_BEGIN("slab: stress cycle");
	{
#define SLAB_STRESS_N 128
		void *ptrs[SLAB_STRESS_N];

		for (int round = 0; round < 5; round++) {
			for (int i = 0; i < SLAB_STRESS_N; i++) {
				/* 交替使用不同大小 */
				size_t sz = 16 << (i % 8);
				ptrs[i] = kmalloc(sz);
				TEST_ASSERT_NOT_NULL(ptrs[i]);
				memset(ptrs[i], (uint8_t)round, sz);
			}
			for (int i = 0; i < SLAB_STRESS_N; i++)
				kfree(ptrs[i]);
		}
#undef SLAB_STRESS_N
	}
	TEST_END("slab: stress cycle");
	return;
fail:
	TEST_FAIL("slab: stress cycle", "see above");
}

void test_slab_returns_empty_page_to_buddy(void)
{
	TEST_BEGIN("slab: returns empty page to buddy");
	{
#define SLAB_RECLAIM_PTRS 512
		void *ptrs[SLAB_RECLAIM_PTRS];
		size_t nr_ptrs = 0;
		size_t free_before;
		size_t free_after_refill;
		size_t free_after_partial;

		free_before = buddy_free_pages();

		while (nr_ptrs < SLAB_RECLAIM_PTRS &&
		       buddy_free_pages() == free_before) {
			ptrs[nr_ptrs] = kmalloc(16);
			TEST_ASSERT_NOT_NULL(ptrs[nr_ptrs]);
			memset(ptrs[nr_ptrs], 0x5a, 16);
			nr_ptrs++;
		}

		TEST_ASSERT(nr_ptrs > 0);
		TEST_ASSERT(buddy_free_pages() < free_before);
		free_after_refill = buddy_free_pages();

		for (size_t i = 0; i + 1 < nr_ptrs; i++)
			kfree(ptrs[i]);

		free_after_partial = buddy_free_pages();
		TEST_ASSERT_EQ(free_after_partial, free_after_refill);

		kfree(ptrs[nr_ptrs - 1]);
		TEST_ASSERT_EQ(buddy_free_pages(), free_before);

		ptrs[0] = kmalloc(16);
		TEST_ASSERT_NOT_NULL(ptrs[0]);
		kfree(ptrs[0]);
		TEST_ASSERT_EQ(buddy_free_pages(), free_before);
#undef SLAB_RECLAIM_PTRS
	}
	TEST_END("slab: returns empty page to buddy");
	return;
fail:
	TEST_FAIL("slab: returns empty page to buddy", "see above");
}

#include <kernel/test.h>
#include <kernel/buddy.h>
#include <kernel/task.h>

#include "ktest.h"

void test_buddy_single_page(void)
{
	TEST_BEGIN("buddy: single page alloc/free");
	{
		void *p = get_free_page(0);
		TEST_ASSERT_NOT_NULL(p);
		TEST_ASSERT_ALIGNED(p, PAGE_SIZE);

		/* 写入模式以验证地址可写 */
		memset(p, 0xBB, PAGE_SIZE);
		TEST_ASSERT(((uint8_t *)p)[0] == 0xBB);
		TEST_ASSERT(((uint8_t *)p)[PAGE_SIZE - 1] == 0xBB);

		free_page(p, 0);
	}
	TEST_END("buddy: single page alloc/free");
	return;
fail:
	TEST_FAIL("buddy: single page alloc/free", "see above");
}

void test_buddy_multi_order(void)
{
	TEST_BEGIN("buddy: multi-order alloc/free");
	{
		void *ptrs[5];

		for (uint32_t order = 0; order <= 4; order++) {
			size_t size = (size_t)PAGE_SIZE << order;
			size_t align = size;

			ptrs[order] = get_free_page(order);
			TEST_ASSERT_NOT_NULL(ptrs[order]);
			TEST_ASSERT_ALIGNED(ptrs[order], align);

			/* 写入首尾字节 */
			memset(ptrs[order], 0xCC, size);
			TEST_ASSERT(((uint8_t *)ptrs[order])[0] == 0xCC);
			TEST_ASSERT(((uint8_t *)ptrs[order])[size - 1] == 0xCC);
		}

		/* 全部释放 */
		for (uint32_t order = 0; order <= 4; order++)
			free_page(ptrs[order], order);
	}
	TEST_END("buddy: multi-order alloc/free");
	return;
fail:
	TEST_FAIL("buddy: multi-order alloc/free", "see above");
}

void test_buddy_merge(void)
{
	TEST_BEGIN("buddy: buddy merging");
	{
		/* 分配 4 个连续 order-0 页 */
		void *p0 = get_free_page(0);
		void *p1 = get_free_page(0);
		void *p2 = get_free_page(0);
		void *p3 = get_free_page(0);

		TEST_ASSERT_NOT_NULL(p0);
		TEST_ASSERT_NOT_NULL(p1);
		TEST_ASSERT_NOT_NULL(p2);
		TEST_ASSERT_NOT_NULL(p3);

		/* 释放全部 */
		free_page(p0, 0);
		free_page(p1, 0);
		free_page(p2, 0);
		free_page(p3, 0);

		/*
		 * 如果合并正常工作，释放 4 个 order-0 块后
		 * 应能分配一个 order-2（4页）块。
		 * 注意：不保证恰好是同一块，但合并后系统应有足够的连续页。
		 */
		void *big = get_free_page(2);
		TEST_ASSERT_NOT_NULL(big);
		TEST_ASSERT_ALIGNED(big, (size_t)PAGE_SIZE << 2);

		free_page(big, 2);
	}
	TEST_END("buddy: buddy merging");
	return;
fail:
	TEST_FAIL("buddy: buddy merging", "see above");
}

void test_buddy_stress(void)
{
	TEST_BEGIN("buddy: stress alloc/free cycle");
	{
#define BUDDY_STRESS_N 64
		void *ptrs[BUDDY_STRESS_N];

		for (int round = 0; round < 3; round++) {
			/* 分配 */
			for (int i = 0; i < BUDDY_STRESS_N; i++) {
				ptrs[i] = get_free_page(0);
				TEST_ASSERT_NOT_NULL(ptrs[i]);
				memset(ptrs[i], (uint8_t)(round + i),
				       PAGE_SIZE);
			}

			/* 释放 */
			for (int i = 0; i < BUDDY_STRESS_N; i++)
				free_page(ptrs[i], 0);
		}
#undef BUDDY_STRESS_N
	}
	TEST_END("buddy: stress alloc/free cycle");
	return;
fail:
	TEST_FAIL("buddy: stress alloc/free cycle", "see above");
}

void test_buddy_split(void)
{
	TEST_BEGIN("buddy: order split");
	{
		/* 分配 order-3 */
		void *big = get_free_page(3);
		TEST_ASSERT_NOT_NULL(big);

		/* 写入模式 */
		memset(big, 0xDD, PAGE_SIZE << 3);
		free_page(big, 3);

		/* 分配 8 个 order-0，应该成功（拆分了刚才释放的块） */
		void *pages[8];
		for (int i = 0; i < 8; i++) {
			pages[i] = get_free_page(0);
			TEST_ASSERT_NOT_NULL(pages[i]);
		}

		for (int i = 0; i < 8; i++)
			free_page(pages[i], 0);
	}
	TEST_END("buddy: order split");
	return;
fail:
	TEST_FAIL("buddy: order split", "see above");
}

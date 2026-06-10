#include <kernel/bitmap.h>
#include <kernel/test.h>

#include "ktest.h"

void test_bitmap(void)
{
	TEST_BEGIN("bitmap: basic set/clear/test");
	{
		BITMAP_DECLARE_STATIC(bm, 64);

		bitmap_zero(&bm);

		/* 所有位应为 0 */
		TEST_ASSERT_EQ(bitmap_test(&bm, 0), false);
		TEST_ASSERT_EQ(bitmap_test(&bm, 31), false);
		TEST_ASSERT_EQ(bitmap_test(&bm, 63), false);

		/* 设置位并验证 */
		bitmap_set(&bm, 0);
		TEST_ASSERT_EQ(bitmap_test(&bm, 0), true);

		bitmap_set(&bm, 31);
		TEST_ASSERT_EQ(bitmap_test(&bm, 31), true);

		bitmap_set(&bm, 63);
		TEST_ASSERT_EQ(bitmap_test(&bm, 63), true);

		/* 未设置的位应仍为 0 */
		TEST_ASSERT_EQ(bitmap_test(&bm, 1), false);
		TEST_ASSERT_EQ(bitmap_test(&bm, 62), false);

		/* 清除位并验证 */
		bitmap_clear(&bm, 31);
		TEST_ASSERT_EQ(bitmap_test(&bm, 31), false);
	}
	TEST_END("bitmap: basic set/clear/test");
	return;
fail:
	TEST_FAIL("bitmap: basic set/clear/test", "see above");
}

void test_bitmap_find_first_zero(void)
{
	TEST_BEGIN("bitmap: find_first_zero");
	{
		BITMAP_DECLARE_STATIC(bm, 64);

		bitmap_zero(&bm);

		/* 全空时第一个 0 位是 0 */
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)0);

		/* 设置位 0 后，第一个 0 位是 1 */
		bitmap_set(&bm, 0);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)1);

		/* 设置位 0~31 后，第一个 0 位是 32 */
		for (size_t i = 0; i < 32; i++)
			bitmap_set(&bm, i);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)32);

		/* 全满时返回 nbits */
		for (size_t i = 0; i < 64; i++)
			bitmap_set(&bm, i);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)64);
	}
	TEST_END("bitmap: find_first_zero");
	return;
fail:
	TEST_FAIL("bitmap: find_first_zero", "see above");
}

void test_bitmap_odd_bits(void)
{
	TEST_BEGIN("bitmap: odd bits set/clear");
	{
		BITMAP_DECLARE_STATIC(bm, 32);

		bitmap_zero(&bm);

		/* 设置所有奇数位 */
		for (size_t i = 1; i < 32; i += 2)
			bitmap_set(&bm, i);

		/* 偶数位应为 0 */
		for (size_t i = 0; i < 32; i += 2)
			TEST_ASSERT_EQ(bitmap_test(&bm, i), false);

		/* 奇数位应为 1 */
		for (size_t i = 1; i < 32; i += 2)
			TEST_ASSERT_EQ(bitmap_test(&bm, i), true);

		/* find_first_zero 应返回 0 */
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)0);
	}
	TEST_END("bitmap: odd bits set/clear");
	return;
fail:
	TEST_FAIL("bitmap: odd bits set/clear", "see above");
}

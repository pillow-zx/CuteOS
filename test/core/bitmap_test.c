#include <kernel/bitmap.h>
#include <kernel/test.h>

#include "../ktest.h"

int test_bitmap(void)
{
	TEST_BEGIN("bitmap: basic set/clear/test");
	{
		BITMAP_DECLARE_STATIC(bm, 64);

		bitmap_zero(&bm);


		TEST_ASSERT_EQ(bitmap_test(&bm, 0), false);
		TEST_ASSERT_EQ(bitmap_test(&bm, 31), false);
		TEST_ASSERT_EQ(bitmap_test(&bm, 63), false);


		bitmap_set(&bm, 0);
		TEST_ASSERT_EQ(bitmap_test(&bm, 0), true);

		bitmap_set(&bm, 31);
		TEST_ASSERT_EQ(bitmap_test(&bm, 31), true);

		bitmap_set(&bm, 63);
		TEST_ASSERT_EQ(bitmap_test(&bm, 63), true);


		TEST_ASSERT_EQ(bitmap_test(&bm, 1), false);
		TEST_ASSERT_EQ(bitmap_test(&bm, 62), false);


		bitmap_clear(&bm, 31);
		TEST_ASSERT_EQ(bitmap_test(&bm, 31), false);
	}
	TEST_END("bitmap: basic set/clear/test");
	return __test_ret;
fail:
	TEST_FAIL("bitmap: basic set/clear/test", "see above");

	return __test_ret;
}

int test_bitmap_find_first_zero(void)
{
	TEST_BEGIN("bitmap: find_first_zero");
	{
		BITMAP_DECLARE_STATIC(bm, 64);

		bitmap_zero(&bm);


		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)0);


		bitmap_set(&bm, 0);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)1);


		for (size_t i = 0; i < 32; i++)
			bitmap_set(&bm, i);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)32);


		for (size_t i = 0; i < 64; i++)
			bitmap_set(&bm, i);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)64);
	}
	TEST_END("bitmap: find_first_zero");
	return __test_ret;
fail:
	TEST_FAIL("bitmap: find_first_zero", "see above");

	return __test_ret;
}

int test_bitmap_odd_bits(void)
{
	TEST_BEGIN("bitmap: odd bits set/clear");
	{
		BITMAP_DECLARE_STATIC(bm, 32);

		bitmap_zero(&bm);


		for (size_t i = 1; i < 32; i += 2)
			bitmap_set(&bm, i);


		for (size_t i = 0; i < 32; i += 2)
			TEST_ASSERT_EQ(bitmap_test(&bm, i), false);


		for (size_t i = 1; i < 32; i += 2)
			TEST_ASSERT_EQ(bitmap_test(&bm, i), true);


		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)0);
	}
	TEST_END("bitmap: odd bits set/clear");
	return __test_ret;
fail:
	TEST_FAIL("bitmap: odd bits set/clear", "see above");

	return __test_ret;
}

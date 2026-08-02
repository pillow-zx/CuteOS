#include <kernel/bitmap.h>
#include <kernel/test.h>

#include "../ktest.h"

int test_bitmap(void)
{
	TEST_BEGIN("bitmap: init and basic set/clear/test");
	{
		BITMAP_DECLARE(map, 130);
		uintptr_t storage[BITMAP_WORDS(128)];
		struct bitmap external;

		TEST_ASSERT_EQ(map.nbits, (size_t)130);
		TEST_ASSERT_EQ(map.nwords, (size_t)BITMAP_WORDS(130));
		TEST_ASSERT_EQ(BITMAP_BYTES(130),
			       sizeof(uintptr_t[BITMAP_WORDS(130)]));

		bitmap_init(&external, storage, 128);
		TEST_ASSERT_EQ(external.nbits, (size_t)128);
		TEST_ASSERT_EQ(external.nwords, (size_t)BITMAP_WORDS(128));
		bitmap_zero(&external);

		TEST_ASSERT_EQ(bitmap_test(&external, 0), false);
		TEST_ASSERT_EQ(bitmap_test(&external, 63), false);
		TEST_ASSERT_EQ(bitmap_test(&external, 127), false);

		bitmap_set(&external, 0);
		bitmap_set(&external, 63);
		bitmap_set(&external, 127);
		TEST_ASSERT_EQ(bitmap_test(&external, 0), true);
		TEST_ASSERT_EQ(bitmap_test(&external, 63), true);
		TEST_ASSERT_EQ(bitmap_test(&external, 127), true);

		bitmap_clear(&external, 63);
		TEST_ASSERT_EQ(bitmap_test(&external, 63), false);
	}
	TEST_END("bitmap: init and basic set/clear/test");
	return __test_ret;
fail:
	TEST_FAIL("bitmap: init and basic set/clear/test", "see above");

	return __test_ret;
}

int test_bitmap_ranges(void)
{
	TEST_BEGIN("bitmap: assign and range operations");
	{
		BITMAP_DECLARE_STATIC(map, 100);

		bitmap_zero(&map);
		bitmap_assign(&map, 7, true);
		bitmap_assign(&map, 8, false);
		TEST_ASSERT_EQ(bitmap_test(&map, 7), true);
		TEST_ASSERT_EQ(bitmap_test(&map, 8), false);

		bitmap_set_range(&map, 5, 70);
		for (size_t i = 0; i < 100; i++) {
			if (i >= 5 && i < 75)
				TEST_ASSERT_EQ(bitmap_test(&map, i), true);
		}

		bitmap_clear_range(&map, 16, 32);
		for (size_t i = 0; i < 100; i++) {
			if (i >= 16 && i < 48)
				TEST_ASSERT_EQ(bitmap_test(&map, i), false);
		}

		bitmap_set_range(&map, 90, 30);
		for (size_t i = 90; i < 100; i++)
			TEST_ASSERT_EQ(bitmap_test(&map, i), true);
		bitmap_clear_range(&map, 100, 10);
	}
	TEST_END("bitmap: assign and range operations");
	return __test_ret;
fail:
	TEST_FAIL("bitmap: assign and range operations", "see above");

	return __test_ret;
}

int test_bitmap_find_first_set(void)
{
	TEST_BEGIN("bitmap: find_first_set");
	{
		BITMAP_DECLARE_STATIC(map, 96);

		bitmap_zero(&map);
		TEST_ASSERT_EQ(bitmap_find_first_set(&map), (size_t)96);

		bitmap_set(&map, 31);
		bitmap_set(&map, 65);
		TEST_ASSERT_EQ(bitmap_find_first_set(&map), (size_t)31);

		bitmap_clear(&map, 31);
		TEST_ASSERT_EQ(bitmap_find_first_set(&map), (size_t)65);
	}
	TEST_END("bitmap: find_first_set");
	return __test_ret;
fail:
	TEST_FAIL("bitmap: find_first_set", "see above");

	return __test_ret;
}

int test_bitmap_find_first_zero(void)
{
	TEST_BEGIN("bitmap: find_first_zero and tail mask");
	{
		BITMAP_DECLARE_STATIC(map, 70);
		BITMAP_DECLARE_STATIC(aligned_map, 128);

		bitmap_zero(&map);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&map), (size_t)0);

		bitmap_fill(&map);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&map), (size_t)70);

		bitmap_clear(&map, 69);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&map), (size_t)69);
		TEST_ASSERT_EQ(bitmap_test(&map, 70), false);

		bitmap_zero(&aligned_map);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&aligned_map), (size_t)0);
		bitmap_fill(&aligned_map);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&aligned_map),
			       (size_t)128);
	}
	TEST_END("bitmap: find_first_zero and tail mask");
	return __test_ret;
fail:
	TEST_FAIL("bitmap: find_first_zero and tail mask", "see above");

	return __test_ret;
}

int test_bitmap_find_next(void)
{
	TEST_BEGIN("bitmap: find_next helpers");
	{
		BITMAP_DECLARE_STATIC(map, 130);

		bitmap_zero(&map);
		TEST_ASSERT_EQ(bitmap_find_next_set(&map, 0), (size_t)130);
		TEST_ASSERT_EQ(bitmap_find_next_zero(&map, 0), (size_t)0);
		TEST_ASSERT_EQ(bitmap_find_next_zero(&map, 129), (size_t)129);
		TEST_ASSERT_EQ(bitmap_find_next_zero(&map, 130), (size_t)130);

		bitmap_set(&map, 5);
		bitmap_set(&map, 64);
		bitmap_set(&map, 129);
		TEST_ASSERT_EQ(bitmap_find_next_set(&map, 0), (size_t)5);
		TEST_ASSERT_EQ(bitmap_find_next_set(&map, 5), (size_t)5);
		TEST_ASSERT_EQ(bitmap_find_next_set(&map, 6), (size_t)64);
		TEST_ASSERT_EQ(bitmap_find_next_set(&map, 65), (size_t)129);
		TEST_ASSERT_EQ(bitmap_find_next_set(&map, 130), (size_t)130);

		bitmap_fill(&map);
		bitmap_clear(&map, 7);
		bitmap_clear(&map, 65);
		bitmap_clear(&map, 129);
		TEST_ASSERT_EQ(bitmap_find_next_zero(&map, 0), (size_t)7);
		TEST_ASSERT_EQ(bitmap_find_next_zero(&map, 7), (size_t)7);
		TEST_ASSERT_EQ(bitmap_find_next_zero(&map, 8), (size_t)65);
		TEST_ASSERT_EQ(bitmap_find_next_zero(&map, 66), (size_t)129);
		TEST_ASSERT_EQ(bitmap_find_next_zero(&map, 130), (size_t)130);
	}
	TEST_END("bitmap: find_next helpers");
	return __test_ret;
fail:
	TEST_FAIL("bitmap: find_next helpers", "see above");

	return __test_ret;
}

int test_bitmap_weight(void)
{
	TEST_BEGIN("bitmap: weight, empty, full, and bounds");
	{
		BITMAP_DECLARE_STATIC(map, 70);

		bitmap_zero(&map);
		TEST_ASSERT_EQ(bitmap_weight(&map), (size_t)0);
		TEST_ASSERT_EQ(bitmap_empty(&map), true);
		TEST_ASSERT_EQ(bitmap_full(&map), false);

		bitmap_set(&map, 0);
		bitmap_set(&map, 7);
		bitmap_set(&map, 31);
		bitmap_set(&map, 69);
		bitmap_set(&map, 70);
		TEST_ASSERT_EQ(bitmap_weight(&map), (size_t)4);
		TEST_ASSERT_EQ(bitmap_empty(&map), false);
		TEST_ASSERT_EQ(bitmap_full(&map), false);

		bitmap_fill(&map);
		TEST_ASSERT_EQ(bitmap_weight(&map), (size_t)70);
		TEST_ASSERT_EQ(bitmap_full(&map), true);
	}
	TEST_END("bitmap: weight, empty, full, and bounds");
	return __test_ret;
fail:
	TEST_FAIL("bitmap: weight, empty, full, and bounds", "see above");

	return __test_ret;
}

int test_bitmap_odd_bits(void)
{
	TEST_BEGIN("bitmap: odd bits set/clear");
	{
		BITMAP_DECLARE_STATIC(map, 32);

		bitmap_zero(&map);
		for (size_t i = 1; i < 32; i += 2)
			bitmap_set(&map, i);

		for (size_t i = 0; i < 32; i += 2)
			TEST_ASSERT_EQ(bitmap_test(&map, i), false);
		for (size_t i = 1; i < 32; i += 2)
			TEST_ASSERT_EQ(bitmap_test(&map, i), true);

		TEST_ASSERT_EQ(bitmap_find_first_set(&map), (size_t)1);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&map), (size_t)0);
	}
	TEST_END("bitmap: odd bits set/clear");
	return __test_ret;
fail:
	TEST_FAIL("bitmap: odd bits set/clear", "see above");

	return __test_ret;
}

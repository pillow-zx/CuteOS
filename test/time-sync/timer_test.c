#include <kernel/errno.h>
#include <kernel/test.h>
#include <kernel/time.h>
#include <kernel/timer.h>

#include "../ktest.h"

int test_timer_mtime(void)
{
	TEST_BEGIN("timer: mtime monotonic");
	{
		uint64_t t0 = arch_timer_now();


		for (int i = 0; i < 1000; i++) {
			uint64_t t1 = arch_timer_now();
			TEST_ASSERT(t1 >= t0);
			t0 = t1;
		}
	}
	TEST_END("timer: mtime monotonic");
	return __test_ret;
fail:
	TEST_FAIL("timer: mtime monotonic", "see above");

	return __test_ret;
}

int test_timer_mtimecmp(void)
{
	TEST_BEGIN("timer: mtimecmp write/read");
	{
		uint64_t now = arch_timer_now();


		arch_timer_set(now + 10000000UL);



		arch_timer_set(now + 100000UL);
	}
	TEST_END("timer: mtimecmp write/read");

	return __test_ret;
}

int test_timer_jiffies(void)
{
	TEST_BEGIN("timer: jiffies initial value");
	{

		uint64_t j = jiffies;
		TEST_ASSERT(j < 1000000UL);
	}
	TEST_END("timer: jiffies initial value");
	return __test_ret;
fail:
	TEST_FAIL("timer: jiffies initial value", "see above");

	return __test_ret;
}

int test_timer_constants(void)
{
	TEST_BEGIN("timer: constants");
	{

		TEST_ASSERT_EQ(10000000UL / 100UL, 100000UL);
		TEST_ASSERT_EQ(100000UL * 100UL, 10000000UL);


		TEST_ASSERT_EQ(100000UL * 1000UL / 10000000UL, 10UL);
	}
	TEST_END("timer: constants");
	return __test_ret;
fail:
	TEST_FAIL("timer: constants", "see above");

	return __test_ret;
}

int test_mtime_deadline_helpers(void)
{
	bool has_timeout = true;
	uint64_t deadline = 1;

	TEST_BEGIN("timer: mtime deadline helpers");
	{
		struct timespec one_sec = {
			.tv_sec = 1,
			.tv_nsec = 0,
		};
		struct timespec invalid_nsec = {
			.tv_sec = 0,
			.tv_nsec = 1000000000LL,
		};
		struct timespec invalid_sec = {
			.tv_sec = -1,
			.tv_nsec = 0,
		};
		struct timespec huge = {
			.tv_sec = (int64_t)(UINT64_MAX / MTIME_FREQ) + 1,
			.tv_nsec = 0,
		};
		uint64_t before;
		uint64_t after;

		TEST_ASSERT_EQ(mtime_deadline_from_timespec(NULL,
							    &has_timeout,
							    &deadline),
			       0);
		TEST_ASSERT(!has_timeout);
		TEST_ASSERT_EQ(deadline, (uint64_t)0);

		before = arch_timer_now();
		TEST_ASSERT_EQ(mtime_deadline_from_timespec(&one_sec,
							    &has_timeout,
							    &deadline),
			       0);
		after = arch_timer_now();
		TEST_ASSERT(has_timeout);
		TEST_ASSERT(deadline >= before + MTIME_FREQ);
		TEST_ASSERT(deadline <= after + MTIME_FREQ);

		TEST_ASSERT_EQ(mtime_deadline_from_timespec(&invalid_nsec,
							    &has_timeout,
							    &deadline),
			       -EINVAL);
		TEST_ASSERT_EQ(mtime_deadline_from_timespec(&invalid_sec,
							    &has_timeout,
							    &deadline),
			       -EINVAL);

		TEST_ASSERT_EQ(mtime_deadline_from_timespec(&huge,
							    &has_timeout,
							    &deadline),
			       0);
		TEST_ASSERT(has_timeout);
		TEST_ASSERT_EQ(deadline, UINT64_MAX);

		TEST_ASSERT_EQ(mtime_deadline_from_ms(-1, &has_timeout,
						      &deadline),
			       0);
		TEST_ASSERT(!has_timeout);
		TEST_ASSERT_EQ(deadline, (uint64_t)0);

		before = arch_timer_now();
		TEST_ASSERT_EQ(mtime_deadline_from_ms(0, &has_timeout,
						      &deadline),
			       0);
		after = arch_timer_now();
		TEST_ASSERT(has_timeout);
		TEST_ASSERT(deadline >= before);
		TEST_ASSERT(deadline <= after);

		before = arch_timer_now();
		TEST_ASSERT_EQ(mtime_deadline_from_ms(25, &has_timeout,
						      &deadline),
			       0);
		after = arch_timer_now();
		TEST_ASSERT(has_timeout);
		TEST_ASSERT(deadline >= before + 250000UL);
		TEST_ASSERT(deadline <= after + 250000UL);
	}
	TEST_END("timer: mtime deadline helpers");
	return __test_ret;
fail:
	TEST_FAIL("timer: mtime deadline helpers", "see above");

	return __test_ret;
}

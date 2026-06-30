/*
 * user/bin/time_test.c - time user ABI tests
 */

#include <ulib.h>

static int time_expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int test_clock_settime_errors(void)
{
	struct timespec valid = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};
	struct timespec invalid_nsec = {
		.tv_sec = 1,
		.tv_nsec = 1000000000L,
	};
	int failed = 0;

	failed +=
		time_expect_ret("realtime unsupported",
				clock_settime(CLOCK_REALTIME, &valid), -EPERM);
	failed += time_expect_ret("monotonic unsettable",
				  clock_settime(CLOCK_MONOTONIC, &valid),
				  -EINVAL);
	failed +=
		time_expect_ret("boottime unsettable",
				clock_settime(CLOCK_BOOTTIME, &valid), -EINVAL);
	failed += time_expect_ret("unknown clock", clock_settime(99, &valid),
				  -EINVAL);
	failed += time_expect_ret("invalid nsec",
				  clock_settime(CLOCK_REALTIME, &invalid_nsec),
				  -EINVAL);
	failed += time_expect_ret("null timespec",
				  clock_settime(CLOCK_REALTIME, NULL), -EFAULT);

	return failed;
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("time_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(void)
{
	int failed = 0;

	report_group("clock_settime error paths", test_clock_settime_errors(),
		     &failed);

	if (failed)
		printf("time_test: %d test group(s) FAILED\n", failed);
	else
		printf("time_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

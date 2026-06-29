/*
 * user/bin/clock_settime_test.c - clock_settime compatibility checks
 */

#include <ulib.h>

static int expect_ret(const char *name, long got, long want)
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

	failed += expect_ret("realtime unsupported",
			     clock_settime(CLOCK_REALTIME, &valid), -EPERM);
	failed += expect_ret("monotonic unsettable",
			     clock_settime(CLOCK_MONOTONIC, &valid),
			     -EINVAL);
	failed += expect_ret("boottime unsettable",
			     clock_settime(CLOCK_BOOTTIME, &valid), -EINVAL);
	failed += expect_ret("unknown clock", clock_settime(99, &valid),
			     -EINVAL);
	failed += expect_ret("invalid nsec",
			     clock_settime(CLOCK_REALTIME, &invalid_nsec),
			     -EINVAL);
	failed += expect_ret("null timespec",
			     clock_settime(CLOCK_REALTIME, NULL), -EFAULT);

	return failed;
}

int main(void)
{
	int failed = 0;

	printf("clock_settime_test: errors ... ");
	if (test_clock_settime_errors())
		failed++;
	else
		printf("PASS\n");

	if (failed)
		printf("clock_settime_test: %d test group(s) FAILED\n",
		       failed);
	else
		printf("clock_settime_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

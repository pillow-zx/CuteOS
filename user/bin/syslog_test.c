/*
 * user/bin/syslog_test.c - syslog probe compatibility checks
 */

#include <ulib.h>

static long klogctl(int type, char *bufp, int len)
{
	return syscall(SYS_syslog, type, (long)bufp, len);
}

static int expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int test_size_buffer(void)
{
	long ret = klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);

	if (ret <= 0) {
		printf("FAIL: size buffer expected positive got %ld\n", ret);
		return 1;
	}

	return 0;
}

static int test_errors(void)
{
	int failed = 0;

	failed += expect_ret("unknown command", klogctl(999, NULL, 0), -EINVAL);
	failed += expect_ret("read all deferred",
			     klogctl(SYSLOG_ACTION_READ_ALL, NULL, 0), -ENOSYS);

	return failed;
}

int main(void)
{
	int failed = 0;

	printf("syslog_test: size buffer ... ");
	if (test_size_buffer())
		failed++;
	else
		printf("PASS\n");

	printf("syslog_test: errors ... ");
	if (test_errors())
		failed++;
	else
		printf("PASS\n");

	if (failed)
		printf("syslog_test: %d test group(s) FAILED\n", failed);
	else
		printf("syslog_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

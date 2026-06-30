/*
 * user/bin/printk_test.c - kernel log user ABI tests
 */

#include <ulib.h>

static long klogctl(int type, char *bufp, int len)
{
	return syscall(SYS_syslog, type, (long)bufp, len);
}

static int printk_expect_ret(const char *name, long got, long want)
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

static int test_syslog_errors(void)
{
	int failed = 0;

	failed += printk_expect_ret("unknown command", klogctl(999, NULL, 0),
				    -EINVAL);
	failed += printk_expect_ret("read all deferred",
				    klogctl(SYSLOG_ACTION_READ_ALL, NULL, 0),
				    -ENOSYS);

	return failed;
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("printk_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(void)
{
	int failed = 0;

	report_group("syslog size buffer", test_size_buffer(), &failed);
	report_group("syslog error paths", test_syslog_errors(), &failed);

	if (failed)
		printf("printk_test: %d test group(s) FAILED\n", failed);
	else
		printf("printk_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

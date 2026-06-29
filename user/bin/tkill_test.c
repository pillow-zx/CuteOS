/*
 * user/bin/tkill_test.c - Linux ABI tkill(2) compatibility test
 */

#include <ulib.h>

static volatile int usr1_count;

static void usr1_handler(int sig)
{
	if (sig == SIGUSR1)
		usr1_count++;
}

static int expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int test_tkill_self_signal(void)
{
	struct sigaction sa;
	long tid = gettid();
	long ret;

	usr1_count = 0;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = usr1_handler;
	ret = sigaction(SIGUSR1, &sa, NULL);
	if (ret != 0)
		return expect_ret("sigaction", ret, 0);

	ret = tkill(tid, 0);
	if (expect_ret("self sig 0", ret, 0))
		return 1;
	if (usr1_count != 0) {
		printf("FAIL: sig 0 delivered signal\n");
		return 1;
	}

	ret = tkill(tid, SIGUSR1);
	if (expect_ret("self SIGUSR1", ret, 0))
		return 1;
	if (usr1_count != 1) {
		printf("FAIL: SIGUSR1 count expected 1 got %d\n", usr1_count);
		return 1;
	}

	return 0;
}

static int test_tkill_errors(void)
{
	long tid = gettid();
	int failed = 0;

	failed += expect_ret("bad signal", tkill(tid, NSIG), -EINVAL);
	failed += expect_ret("bad tid", tkill(-1, 0), -EINVAL);
	failed += expect_ret("missing tid", tkill(999999, 0), -ESRCH);

	return failed;
}

int main(void)
{
	int failed = 0;

	printf("tkill_test: self signal ... ");
	if (test_tkill_self_signal())
		failed++;
	else
		printf("PASS\n");

	printf("tkill_test: errors ... ");
	if (test_tkill_errors())
		failed++;
	else
		printf("PASS\n");

	if (failed)
		printf("tkill_test: %d test(s) FAILED\n", failed);
	else
		printf("tkill_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

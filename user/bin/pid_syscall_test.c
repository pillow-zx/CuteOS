/*
 * user/bin/pid_syscall_test.c - PID signedness syscall ABI checks
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

static int test_wait_any_child(void)
{
	int status = 0;
	long pid = fork();
	long waited;

	if (pid < 0)
		return expect_ret("fork", pid, 0);
	if (pid == 0)
		exit(7);

	waited = wait4(-1, &status, 0, NULL);
	if (waited != pid) {
		printf("FAIL: wait4(-1) expected child %ld got %ld\n", pid,
		       waited);
		return 1;
	}
	if (status != (7 << 8)) {
		printf("FAIL: wait status expected %d got %d\n", 7 << 8,
		       status);
		return 1;
	}

	return 0;
}

static int test_pid_error_paths(void)
{
	unsigned long mask = 0;
	struct rlimit64 limit;
	int failed = 0;

	failed += expect_ret("wait4 zero", wait4(0, NULL, 0, NULL), -EINVAL);
	failed += expect_ret("wait4 negative", wait4(-2, NULL, 0, NULL),
			     -EINVAL);
	failed += expect_ret("sched_getaffinity negative",
			     sched_getaffinity(-1, sizeof(mask), &mask),
			     -ESRCH);
	failed += expect_ret("prlimit64 negative",
			     prlimit64(-1, RLIMIT_NOFILE, NULL, &limit),
			     -ESRCH);

	return failed;
}

int main(void)
{
	int failed = 0;

	printf("pid_syscall_test: wait any child ... ");
	if (test_wait_any_child())
		failed++;
	else
		printf("PASS\n");

	printf("pid_syscall_test: pid errors ... ");
	if (test_pid_error_paths())
		failed++;
	else
		printf("PASS\n");

	if (failed)
		printf("pid_syscall_test: %d test(s) FAILED\n", failed);
	else
		printf("pid_syscall_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

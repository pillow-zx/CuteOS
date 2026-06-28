/*
 * user/bin/futex_clone_test.c - futex/clone ABI boundary checks
 */

#include <ulib.h>
#include <uapi/mman.h>
#include <uapi/sched.h>

#define STACK_SIZE (16 * 1024UL)

static int expect_eq(const char *name, long got, long want)
{
	if (got == want)
		return 0;

	printf("FAIL: %s: got %ld, want %ld\n", name, got, want);
	return 1;
}

static int test_futex_error_paths(void)
{
	int failed = 0;
	int word = 1;
	struct timespec timeout;

	failed += expect_eq("futex wait mismatch",
			    futex(&word, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0,
				  NULL, NULL, 0),
			    -EAGAIN);

	word = 0;
	failed += expect_eq("futex bad timeout pointer",
			    futex(&word, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0,
				  (void *)~0UL, NULL, 0),
			    -EFAULT);

	timeout.tv_sec = 0;
	timeout.tv_nsec = 1000000000L;
	failed += expect_eq("futex invalid timeout nsec",
			    futex(&word, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0,
				  &timeout, NULL, 0),
			    -EINVAL);

	timeout.tv_sec = 0;
	timeout.tv_nsec = 0;
	failed += expect_eq("futex zero timeout",
			    futex(&word, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0,
				  &timeout, NULL, 0),
			    -ETIMEDOUT);

	return failed;
}

static int test_robust_list_roundtrip(void)
{
	struct robust_list_head head;
	struct robust_list_head *got = NULL;
	long len = 0;
	int failed = 0;

	memset(&head, 0, sizeof(head));
	head.list.next = &head.list;

	failed += expect_eq("set_robust_list",
			    set_robust_list(&head, sizeof(head)), 0);
	failed += expect_eq("get_robust_list",
			    get_robust_list(0, &got, &len), 0);

	if (got != &head) {
		printf("FAIL: robust head mismatch\n");
		failed++;
	}
	if (len != (long)sizeof(head)) {
		printf("FAIL: robust len got %ld, want %ld\n",
		       len, (long)sizeof(head));
		failed++;
	}

	return failed;
}

static int test_clone_bad_parent_tid(void)
{
	void *stack;
	long ret;
	unsigned long flags;

	stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)stack < 0) {
		printf("FAIL: mmap stack: %ld\n", (long)stack);
		return 1;
	}

	flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD | CLONE_FILES |
		CLONE_FS | CLONE_PARENT_SETTID;
	ret = clone(flags, (char *)stack + STACK_SIZE, (int *)~0UL, 0, NULL);
	munmap(stack, STACK_SIZE);

	return expect_eq("clone bad parent_tid", ret, -EFAULT);
}

int main(int argc, char **argv)
{
	int failed = 0;
	int ret;

	(void)argc;
	(void)argv;

	printf("futex_clone_test: futex error paths ... ");
	ret = test_futex_error_paths();
	failed += ret;
	if (!ret)
		printf("PASS\n");

	printf("futex_clone_test: robust list roundtrip ... ");
	ret = test_robust_list_roundtrip();
	failed += ret;
	if (!ret)
		printf("PASS\n");

	printf("futex_clone_test: clone bad parent_tid ... ");
	ret = test_clone_bad_parent_tid();
	failed += ret;
	if (!ret)
		printf("PASS\n");

	if (failed)
		printf("futex_clone_test: %d test(s) FAILED\n", failed);
	else
		printf("futex_clone_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

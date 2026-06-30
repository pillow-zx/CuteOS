/*
 * user/bin/signal_test.c - signal user ABI tests
 */

#include <ulib.h>
#include <uapi/mman.h>
#include <uapi/signal.h>

#define ALT_STACK_SIZE (SIGSTKSZ * 2)
#define PAGE_SIZE      4096UL

static char *alt_base;
static volatile int handler_ran;
static volatile int handler_on_altstack;
static volatile int handler_saw_onstack;
static volatile int handler_change_denied;

static void handler_check_stack(int sig)
{
	unsigned long sp;
	struct stack_t old;
	struct stack_t disabled;

	(void)sig;
	/* Read current stack pointer. */
	__asm__ volatile("mv %0, sp" : "=r"(sp));

	handler_ran = 1;
	/* Check SP is within [alt_base, alt_base + ALT_STACK_SIZE). */
	if (sp >= (unsigned long)alt_base &&
	    sp < (unsigned long)alt_base + ALT_STACK_SIZE)
		handler_on_altstack = 1;

	if (sigaltstack(NULL, &old) == 0 && (old.ss_flags & SS_ONSTACK))
		handler_saw_onstack = 1;

	disabled.ss_sp = NULL;
	disabled.ss_flags = SS_DISABLE;
	disabled.ss_size = 0;
	if (sigaltstack(&disabled, NULL) == -1)
		handler_change_denied = 1;
}

/* test 1: install alternate stack and verify return is 0 */
static int test_install_altstack(void)
{
	struct stack_t ss;
	long ret;

	alt_base = mmap(NULL, ALT_STACK_SIZE, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)alt_base < 0) {
		printf("FAIL: mmap altstack: %ld\n", (long)alt_base);
		return 1;
	}

	ss.ss_sp = alt_base;
	ss.ss_flags = 0;
	ss.ss_size = ALT_STACK_SIZE;

	ret = sigaltstack(&ss, NULL);
	if (ret != 0) {
		printf("FAIL: sigaltstack install: %ld\n", ret);
		return 1;
	}
	return 0;
}

/* test 2: query current altstack state */
static int test_query_altstack(void)
{
	struct stack_t old;
	long ret;

	old.ss_sp = NULL;
	old.ss_flags = -1;
	old.ss_size = 0;

	ret = sigaltstack(NULL, &old);
	if (ret != 0) {
		printf("FAIL: sigaltstack query: %ld\n", ret);
		return 1;
	}
	if (old.ss_sp != alt_base) {
		printf("FAIL: query ss_sp mismatch: got %p want %p\n",
		       old.ss_sp, alt_base);
		return 1;
	}
	if (old.ss_size != ALT_STACK_SIZE) {
		printf("FAIL: query ss_size mismatch: got %lu want %lu\n",
		       old.ss_size, (unsigned long)ALT_STACK_SIZE);
		return 1;
	}
	return 0;
}

/* test 3: SA_ONSTACK handler runs on alternate stack */
static int test_handler_on_altstack(void)
{
	struct sigaction sa;
	struct stack_t old;

	handler_ran = 0;
	handler_on_altstack = 0;
	handler_saw_onstack = 0;
	handler_change_denied = 0;

	sa.sa_handler = handler_check_stack;
	sa.sa_flags = SA_ONSTACK;
	sa.sa_mask = 0;
	sa.sa_restorer = NULL;

	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		printf("FAIL: sigaction SA_ONSTACK\n");
		return 1;
	}

	raise(SIGUSR1);

	if (!handler_ran) {
		printf("FAIL: handler did not run\n");
		return 1;
	}
	if (!handler_on_altstack) {
		printf("FAIL: handler ran but not on alternate stack\n");
		return 1;
	}
	if (!handler_saw_onstack) {
		printf("FAIL: sigaltstack query did not report SS_ONSTACK\n");
		return 1;
	}
	if (!handler_change_denied) {
		printf("FAIL: sigaltstack change on stack was not denied\n");
		return 1;
	}
	if (sigaltstack(NULL, &old) != 0) {
		printf("FAIL: sigaltstack query after handler\n");
		return 1;
	}
	if (old.ss_flags & SS_ONSTACK) {
		printf("FAIL: SS_ONSTACK still set after handler\n");
		return 1;
	}
	return 0;
}

/* test 4: fork inherits the alternate stack and clears SS_ONSTACK */
static int test_fork_inherits_altstack(void)
{
	struct sigaction sa;
	long pid;
	int status;

	sa.sa_handler = handler_check_stack;
	sa.sa_flags = SA_ONSTACK;
	sa.sa_mask = 0;
	sa.sa_restorer = NULL;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		printf("FAIL: sigaction fork inherit\n");
		return 1;
	}

	pid = fork();
	if (pid == 0) {
		handler_ran = 0;
		handler_on_altstack = 0;
		handler_saw_onstack = 0;
		handler_change_denied = 0;

		raise(SIGUSR1);
		if (!handler_ran)
			exit(10);
		if (!handler_on_altstack)
			exit(11);
		if (!handler_saw_onstack)
			exit(12);
		if (!handler_change_denied)
			exit(13);
		exit(0);
	}
	if (pid < 0) {
		printf("FAIL: fork: %ld\n", pid);
		return 1;
	}
	if (wait4(pid, &status, 0, NULL) != pid) {
		printf("FAIL: wait4 fork altstack\n");
		return 1;
	}
	if (status != 0) {
		printf("FAIL: fork altstack child status=0x%x\n", status);
		return 1;
	}

	return 0;
}

/* test 5: SS_DISABLE prevents use of alternate stack */
static int test_disable_altstack(void)
{
	struct stack_t ss, old;
	long ret;

	ss.ss_sp = NULL;
	ss.ss_flags = SS_DISABLE;
	ss.ss_size = 0;

	ret = sigaltstack(&ss, &old);
	if (ret != 0) {
		printf("FAIL: sigaltstack SS_DISABLE: %ld\n", ret);
		return 1;
	}
	/* Query must now show SS_DISABLE. */
	sigaltstack(NULL, &old);
	if (old.ss_flags != SS_DISABLE) {
		printf("FAIL: after disable, flags=%d (want SS_DISABLE=%d)\n",
		       old.ss_flags, SS_DISABLE);
		return 1;
	}
	return 0;
}

/* test 6: invalid flags return -EINVAL */
static int test_invalid_flags(void)
{
	struct stack_t ss;
	long ret;

	ss.ss_sp = alt_base;
	ss.ss_flags = 0xff; /* invalid flags */
	ss.ss_size = ALT_STACK_SIZE;

	ret = sigaltstack(&ss, NULL);
	if (ret != -22) { /* -EINVAL */
		printf("FAIL: invalid flags: expected -22 got %ld\n", ret);
		return 1;
	}
	return 0;
}

static volatile int usr1_count;

static void usr1_handler(int sig)
{
	if (sig == SIGUSR1)
		usr1_count++;
}

static int signal_expect_ret(const char *name, long got, long want)
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
		return signal_expect_ret("sigaction", ret, 0);

	ret = tkill(tid, 0);
	if (signal_expect_ret("self sig 0", ret, 0))
		return 1;
	if (usr1_count != 0) {
		printf("FAIL: sig 0 delivered signal\n");
		return 1;
	}

	ret = tkill(tid, SIGUSR1);
	if (signal_expect_ret("self SIGUSR1", ret, 0))
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

	failed += signal_expect_ret("bad signal", tkill(tid, NSIG), -EINVAL);
	failed += signal_expect_ret("bad tid", tkill(-1, 0), -EINVAL);
	failed += signal_expect_ret("missing tid", tkill(999999, 0), -ESRCH);

	return failed;
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("signal_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(void)
{
	int failed = 0;

	report_group("install altstack", test_install_altstack(), &failed);
	report_group("query altstack", test_query_altstack(), &failed);
	report_group("handler on altstack", test_handler_on_altstack(),
		     &failed);
	report_group("fork inherits altstack", test_fork_inherits_altstack(),
		     &failed);
	report_group("disable altstack", test_disable_altstack(), &failed);
	report_group("invalid altstack flags", test_invalid_flags(), &failed);
	report_group("tkill self signal", test_tkill_self_signal(), &failed);
	report_group("tkill error paths", test_tkill_errors(), &failed);

	if (failed)
		printf("signal_test: %d test group(s) FAILED\n", failed);
	else
		printf("signal_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

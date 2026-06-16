#include <ulib.h>

static volatile int got_usr1;
static volatile int got_usr2;

static void usr1_handler(int sig)
{
	got_usr1 = sig;
}

static void usr2_handler(int sig)
{
	got_usr2 = sig;
}

/* run - 以名称执行单个测试并即时打印结果，避免失败时只能看到汇总计数。 */
static int run(const char *name, int (*test)(void))
{
	int result = test();

	printf("%s", name);
	printf("%s", result == 0 ? ": OK\n" : ": FAIL\n");
	return result;
}

static int test_handler_return(void)
{
	struct sigaction act;

	act.sa_handler = usr1_handler;
	act.sa_flags = 0;
	act.sa_restorer = 0;
	act.sa_mask = 0;

	if (sigaction(SIGUSR1, &act, 0) != 0) {
		printf("%s", "  sigaction failed\n");
		return 1;
	}
	if (kill(getpid(), SIGUSR1) != 0) {
		printf("%s", "  kill failed\n");
		return 1;
	}
	if (got_usr1 != SIGUSR1) {
		printf("%s", "  handler did not run (expected SIGUSR1, got ");
		printf("%ld", (long)(got_usr1));
		printf("%s", ")\n");
		return 1;
	}
	return 0;
}

static int test_block_unblock(void)
{
	struct sigaction act;
	unsigned long mask = 1UL << (SIGUSR2 - 1);

	act.sa_handler = usr2_handler;
	act.sa_flags = 0;
	act.sa_restorer = 0;
	act.sa_mask = 0;

	if (sigaction(SIGUSR2, &act, 0) != 0) {
		printf("%s", "  sigaction failed\n");
		return 1;
	}
	if (sigprocmask(SIG_BLOCK, &mask, 0) != 0) {
		printf("%s", "  sigprocmask BLOCK failed\n");
		return 1;
	}
	if (kill(getpid(), SIGUSR2) != 0) {
		printf("%s", "  kill failed\n");
		return 1;
	}
	if (got_usr2 != 0) {
		printf("%s", "  handler ran while blocked\n");
		return 1;
	}
	if (sigprocmask(SIG_UNBLOCK, &mask, 0) != 0) {
		printf("%s", "  sigprocmask UNBLOCK failed\n");
		return 1;
	}
	if (got_usr2 != SIGUSR2) {
		printf("%s", "  handler did not run after unblock\n");
		return 1;
	}
	return 0;
}

static int test_ignore(void)
{
	struct sigaction act;

	got_usr1 = 0;
	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	act.sa_restorer = 0;
	act.sa_mask = 0;

	if (sigaction(SIGUSR1, &act, 0) != 0) {
		printf("%s", "  sigaction failed\n");
		return 1;
	}
	if (kill(getpid(), SIGUSR1) != 0) {
		printf("%s", "  kill failed\n");
		return 1;
	}
	if (got_usr1 != 0) {
		printf("%s", "  handler ran despite SIG_IGN\n");
		return 1;
	}
	return 0;
}

static int test_uncatchable(void)
{
	struct sigaction act;

	act.sa_handler = usr1_handler;
	act.sa_flags = 0;
	act.sa_restorer = 0;
	act.sa_mask = 0;

	if (sigaction(SIGKILL, &act, 0) >= 0) {
		printf("%s", "  SIGKILL was incorrectly catchable\n");
		return 1;
	}
	return 0;
}

static int test_default_terminate(void)
{
	long child = fork();

	if (child < 0) {
		printf("%s", "  fork failed\n");
		return 1;
	}
	if (child == 0) {
		while (1)
			yield();
	}

	if (kill(child, SIGTERM) != 0) {
		printf("%s", "  kill failed\n");
		return 1;
	}

	int status = -1;
	long waited = wait4(child, &status, 0, 0);
	if (waited != child) {
		printf("%s", "  wait4 returned wrong pid\n");
		return 1;
	}
	if (status != (SIGNAL_EXIT_CODE(SIGTERM) << 8)) {
		printf("%s", "  unexpected status: expected ");
		printf("0x%lx",
		       (unsigned long)(SIGNAL_EXIT_CODE(SIGTERM) << 8));
		printf("%s", " got ");
		printf("0x%lx", (unsigned long)((unsigned long)status));
		printf("%s", "\n");
		return 1;
	}
	return 0;
}

static int test_page_fault_sigsegv(void)
{
	long child = fork();

	if (child < 0) {
		printf("%s", "  fork failed\n");
		return 1;
	}
	if (child == 0) {
		volatile char *bad = (volatile char *)0;

		*bad = 1; /* NULL 解引用：应触发 SIGSEGV 默认终止 */
		exit(88);
	}

	int status = -1;
	long waited = wait4(child, &status, 0, 0);
	if (waited != child) {
		printf("%s", "  wait4 returned wrong pid\n");
		return 1;
	}
	if (status != (SIGNAL_EXIT_CODE(SIGSEGV) << 8)) {
		printf("%s", "  unexpected status: expected ");
		printf("0x%lx",
		       (unsigned long)(SIGNAL_EXIT_CODE(SIGSEGV) << 8));
		printf("%s", " got ");
		printf("0x%lx", (unsigned long)((unsigned long)status));
		printf("%s", "\n");
		return 1;
	}
	return 0;
}

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;

	int failures = 0;

	printf("%s", "=== CuteOS Signal Test ===\n");

	failures += run("handler_return", test_handler_return);
	failures += run("block_unblock", test_block_unblock);
	failures += run("ignore", test_ignore);
	failures += run("uncatchable", test_uncatchable);
	failures += run("default_terminate", test_default_terminate);
	failures += run("page_fault_sigsegv", test_page_fault_sigsegv);

	if (failures == 0)
		printf("%s", "=== Signal tests passed ===\n");
	else {
		printf("%s", "=== Signal tests failed: ");
		printf("%ld", (long)(failures));
		printf("%s", " ===\n");
	}

	return failures == 0 ? 0 : 1;
}

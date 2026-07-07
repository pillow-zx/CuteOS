#include <ulib.h>

static const char *tests[] = {
	"fd_test",   "fs_test",	    "mm_test",	"poll_test", "printk_test",
	"rseq_test", "signal_test", "smp_test", "task_test", "time_test",
};

static int run_test(const char *name)
{
	char path[PATH_MAX];
	char *argv[] = {(char *)name, NULL};
	char *envp[] = {NULL};
	long pid;
	long waited;
	int status = 0;

	if (snprintf(path, sizeof(path), "/bin/%s", name) < 0) {
		printf("test_all: %s: path format failed\n", name);
		return 1;
	}

	printf("test_all: running %s\n", name);

	pid = fork();
	if (pid < 0) {
		printf("test_all: fork %s failed: %ld\n", name, pid);
		return 1;
	}

	if (pid == 0) {
		execve(path, argv, envp);
		printf("test_all: exec %s failed\n", path);
		exit(127);
	}

	waited = wait4(pid, &status, 0, NULL);
	if (waited != pid) {
		printf("test_all: wait %s expected %ld got %ld\n", name, pid,
		       waited);
		return 1;
	}

	if (status != 0) {
		printf("test_all: %s FAILED status=0x%x\n", name, status);
		return 1;
	}

	printf("test_all: %s PASSED\n", name);
	return 0;
}

int main(void)
{
	int failed = 0;
	unsigned long i;

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
		failed += run_test(tests[i]);

	if (failed)
		printf("test_all: %d test program(s) FAILED\n", failed);
	else
		printf("test_all: all test programs PASSED\n");

	return failed ? 1 : 0;
}

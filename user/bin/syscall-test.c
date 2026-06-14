#include <ulib.h>

static void print_argv_envp(int argc, char **argv, char **envp)
{
	print("[TEST] argc = ");
	print_hex((unsigned long)argc);
	print("\n");

	for (int i = 0; i < argc; i++) {
		print("[TEST] argv[");
		print_hex((unsigned long)i);
		print("] = ");
		print(argv[i]);
		print("\n");
	}

	for (int i = 0; envp && envp[i] != NULL; i++) {
		print("[TEST] envp[");
		print_hex((unsigned long)i);
		print("] = ");
		print(envp[i]);
		print("\n");
	}
}

static int test_basic_syscalls(void)
{
	int failures = 0;
	long pid = getpid();

	print("[TEST] getpid = ");
	print_hex((unsigned long)pid);
	print("\n");
	if (pid <= 1)
		failures++;

	print("[TEST] getppid = ");
	print_hex((unsigned long)getppid());
	print(", uid = ");
	print_hex((unsigned long)getuid());
	print(", gid = ");
	print_hex((unsigned long)getgid());
	print("\n");

	print("[TEST] write: OK\n");
	print("[TEST] yield...\n");
	yield();
	print("[TEST] yield: returned OK\n");

	long initial_brk = brk(0);
	long new_brk = brk(initial_brk + 4096);

	print("[TEST] brk extend = ");
	print_hex((unsigned long)new_brk);
	print("\n");
	if (new_brk == initial_brk + 4096) {
		volatile char *heap = (volatile char *)initial_brk;

		heap[0] = 0x42;
		heap[100] = 0x43;
		if (heap[0] != 0x42 || heap[100] != 0x43)
			failures++;
	} else {
		failures++;
	}

	char *map = mmap(0, 8192, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	print("[TEST] mmap anonymous = ");
	print_hex((unsigned long)map);
	print("\n");
	if ((long)map < 0) {
		failures++;
	} else {
		map[0] = 0x55;
		map[4096] = 0x66;
		if (map[0] != 0x55 || map[4096] != 0x66)
			failures++;
		if (munmap(map, 8192) != 0)
			failures++;
	}

	return failures;
}

static int test_dup(void)
{
	int failures = 0;
	long dup_fd = dup(1);

	if (dup_fd >= 0) {
		write((int)dup_fd, "[TEST] dup stdout: OK\n", 22);
		close((int)dup_fd);
	} else {
		failures++;
	}

	long dup2_fd = dup2(1, 5);
	if (dup2_fd == 5) {
		write(5, "[TEST] dup2 stdout: OK\n", 23);
		close(5);
	} else {
		failures++;
	}

	return failures;
}

static int test_dev_null(void)
{
	long fd = open("/dev/null", O_WRONLY);
	long n;

	print("[TEST] open /dev/null = ");
	print_long(fd);
	print("\n");
	if (fd < 0)
		return 1;

	n = write((int)fd, "discard", 7);
	close((int)fd);
	return n == 7 ? 0 : 1;
}

static int test_pipe(void)
{
	int pipefd[2];

	print("[TEST] pipe parent->child...\n");
	if (pipe(pipefd) != 0)
		return 1;

	long child = fork();
	if (child == 0) {
		char buf[8];
		long n;

		close(pipefd[1]);
		n = read(pipefd[0], buf, 5);
		if (n >= 0 && n < (long)sizeof(buf))
			buf[n] = '\0';
		else
			buf[0] = '\0';
		close(pipefd[0]);
		exit(n == 5 && streq(buf, "ping!") ? 11 : 12);
	}
	if (child < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return 1;
	}

	close(pipefd[0]);
	write(pipefd[1], "ping!", 5);
	close(pipefd[1]);

	int status = -1;
	long waited = wait4(child, &status, 0, 0);

	print("[TEST] pipe child status = ");
	print_hex((unsigned long)status);
	print("\n");

	return waited == child && status == 0xb00 ? 0 : 1;
}

static int test_fork_exec_wait(void)
{
	print("[TEST] fork/exec/wait...\n");

	long child = fork();
	if (child == 0) {
		char *child_argv[] = {
			"syscall-test",
			"exec-child",
			"argv-ok",
			0,
		};
		char *child_envp[] = {
			"CUTEOS_ENV=ok",
			0,
		};
		long ret = execve("/bin/syscall-test", child_argv, child_envp);

		print("[CHILD] execve FAILED, ret=");
		print_long(ret);
		print("\n");
		exit(2);
	}
	if (child < 0)
		return 1;

	int status = -1;
	long waited = wait4(child, &status, 0, 0);

	print("[TEST] exec child status = ");
	print_hex((unsigned long)status);
	print("\n");
	return waited == child && status == 0x700 ? 0 : 1;
}

int main(int argc, char **argv, char **envp)
{
	int failures = 0;

	if (argc > 1 && streq(argv[1], "exec-child")) {
		print("[EXEC-CHILD] execve replaced the fork child\n");
		if (argc > 2 && streq(argv[2], "argv-ok") && envp && envp[0] &&
		    streq(envp[0], "CUTEOS_ENV=ok"))
			return 7;
		return 8;
	}

	print("=== CuteOS Syscall Test ===\n");
	print_argv_envp(argc, argv, envp);

	failures += test_basic_syscalls();
	failures += test_dup();
	failures += test_dev_null();
	failures += test_pipe();
	failures += test_fork_exec_wait();

	if (failures == 0) {
		print("=== All tests passed ===\n");
	} else {
		print("=== Tests failed: ");
		print_long(failures);
		print(" ===\n");
	}

	return failures == 0 ? 0 : 1;
}

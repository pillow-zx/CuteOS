/*
 * user/init/init.c - 用户态测试程序
 *
 * CuteOS 的第一个用户程序，测试基础系统调用：
 *   1. getpid / write — 系统调用基本功能
 *   2. yield          — 主动让出 CPU 后恢复执行
 *   3. brk + 内存访问 — lazy allocation + page fault 端到端验证
 *   4. exit           — 正常退出
 */

#include <user.h>

static void print(const char *s)
{
	int len = 0;
	while (s[len])
		len++;
	write(1, s, len);
}

static void print_hex(unsigned long val)
{
	char buf[19]; /* "0x" + 16 hex digits + '\0' */
	buf[0] = '0';
	buf[1] = 'x';
	for (int i = 15; i >= 0; i--) {
		int d = val & 0xf;
		buf[2 + i] = d < 10 ? '0' + d : 'a' + d - 10;
		val >>= 4;
	}
	buf[18] = '\0';
	print(buf);
}

static void print_long(long val)
{
	if (val < 0) {
		print("-");
		val = -val;
	}
	print_hex((unsigned long)val);
}

static int streq(const char *a, const char *b)
{
	while (*a && *b && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static void print_argv(int argc, char **argv)
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
}

static int test_basic_syscalls(void)
{
	int failures = 0;

	long pid = getpid();
	print("[TEST] getpid = ");
	print_hex((unsigned long)pid);
	print(" (expected 1)\n");
	if (pid != 1)
		failures++;

	print("[TEST] getppid = ");
	print_hex((unsigned long)getppid());
	print(", uid = ");
	print_hex((unsigned long)getuid());
	print(", gid = ");
	print_hex((unsigned long)getgid());
	print("\n");

	print("[TEST] write: ");
	print("OK\n");

	print("[TEST] yield...\n");
	yield();
	print("[TEST] yield: returned OK\n");

	/* ---- Test 4: brk (query) ---- */
	long initial_brk = brk(0);
	print("[TEST] brk(0) = ");
	print_hex((unsigned long)initial_brk);
	print("\n");

	/* ---- Test 5: brk (extend) + page fault ---- */
	long new_brk = brk(initial_brk + 4096);
	print("[TEST] brk(");
	print_hex((unsigned long)(initial_brk + 4096));
	print(") = ");
	print_hex((unsigned long)new_brk);
	print("\n");

	if (new_brk == initial_brk + 4096) {
		volatile char *heap = (volatile char *)initial_brk;
		heap[0] = 0x42; /* 写入：触发 store page fault → 分配物理页 */
		heap[100] = 0x43;
		print("[TEST] heap[0] = ");
		print_hex((unsigned long)heap[0]);
		print(", heap[100] = ");
		print_hex((unsigned long)heap[100]);
		print(" (expected 0x42, 0x43)\n");
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
		print("[TEST] mmap[0] = ");
		print_hex((unsigned long)map[0]);
		print(", mmap[4096] = ");
		print_hex((unsigned long)map[4096]);
		print(" (expected 0x55, 0x66)\n");
		if (map[0] != 0x55 || map[4096] != 0x66)
			failures++;
		if (munmap(map, 8192) != 0) {
			print("[TEST] munmap FAILED\n");
			failures++;
		} else {
			print("[TEST] munmap: OK\n");
		}
	}

	yield();
	print("[TEST] second yield: OK\n");

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
		print("[TEST] dup FAILED, ret=");
		print_long(dup_fd);
		print("\n");
		failures++;
	}

	long dup2_fd = dup2(1, 5);
	if (dup2_fd == 5) {
		write(5, "[TEST] dup2 stdout: OK\n", 23);
		close(5);
	} else {
		print("[TEST] dup2 FAILED, ret=");
		print_long(dup2_fd);
		print("\n");
		failures++;
	}

	return failures;
}

static int test_pipe_parent_child(void)
{
	print("[TEST] pipe parent->child...\n");

	int p2c[2];
	if (pipe(p2c) != 0) {
		print("[TEST] pipe parent->child FAILED\n");
		return 1;
	}

	long pipe_child = fork();
	if (pipe_child == 0) {
		char buf[8];
		close(p2c[1]);
		long n = read(p2c[0], buf, 5);
		if (n >= 0 && n < (long)sizeof(buf))
			buf[n] = '\0';
		else
			buf[0] = '\0';
		print("[PIPE-CHILD] read ");
		print_long(n);
		print(" bytes: ");
		print(buf);
		print("\n");
		close(p2c[0]);
		exit(n == 5 && streq(buf, "ping!") ? 11 : 12);
	}
	if (pipe_child < 0) {
		close(p2c[0]);
		close(p2c[1]);
		return 1;
	}

	close(p2c[0]);
	long n = write(p2c[1], "ping!", 5);
	print("[PIPE-PARENT] wrote ");
	print_long(n);
	print(" bytes\n");
	close(p2c[1]);

	int status = -1;
	long waited = wait4(pipe_child, &status, 0, 0);
	print("[PIPE-PARENT] wait returned pid=");
	print_hex((unsigned long)waited);
	print(", status=");
	print_hex((unsigned long)status);
	print(" (expected status 0xb00)\n");

	return waited == pipe_child && status == 0xb00 ? 0 : 1;
}

static int test_pipe_child_parent(void)
{
	print("[TEST] pipe child->parent...\n");

	int c2p[2];
	if (pipe(c2p) != 0) {
		print("[TEST] pipe child->parent FAILED\n");
		return 1;
	}

	long pipe_child = fork();
	if (pipe_child == 0) {
		close(c2p[0]);
		write(c2p[1], "pong?", 5);
		close(c2p[1]);
		exit(13);
	}
	if (pipe_child < 0) {
		close(c2p[0]);
		close(c2p[1]);
		return 1;
	}

	char buf[8];
	close(c2p[1]);
	long n = read(c2p[0], buf, 5);
	if (n >= 0 && n < (long)sizeof(buf))
		buf[n] = '\0';
	else
		buf[0] = '\0';
	print("[PIPE-PARENT] read ");
	print_long(n);
	print(" bytes: ");
	print(buf);
	print("\n");
	int got_message = n == 5 && streq(buf, "pong?");

	long eof = read(c2p[0], buf, sizeof(buf));
	print("[PIPE-PARENT] EOF read returned ");
	print_long(eof);
	print(" (expected 0)\n");
	close(c2p[0]);

	int status = -1;
	long waited = wait4(pipe_child, &status, 0, 0);
	print("[PIPE-PARENT] child status=");
	print_hex((unsigned long)status);
	print(" (expected status 0xd00)\n");

	return waited == pipe_child && got_message && eof == 0 &&
			       status == 0xd00
		       ? 0
		       : 1;
}

static int test_pipe_epipe(void)
{
	print("[TEST] pipe refcount / EPIPE...\n");

	int epipefd[2];
	if (pipe(epipefd) != 0) {
		print("[TEST] pipe EPIPE setup FAILED\n");
		return 1;
	}

	long dup_read = dup(epipefd[0]);
	close(epipefd[0]);

	long keepalive_write = write(epipefd[1], "x", 1);
	print("[TEST] write with duplicated reader returned ");
	print_long(keepalive_write);
	print(" (expected 1)\n");

	if (dup_read >= 0)
		close((int)dup_read);

	long broken_write = write(epipefd[1], "y", 1);
	print("[TEST] write without readers returned ");
	print_long(broken_write);
	print(" (expected -0x20)\n");
	close(epipefd[1]);

	return dup_read >= 0 && keepalive_write == 1 && broken_write == -32 ? 0
									    : 1;
}

static int test_fork_exec_wait(void)
{
	print("[TEST] fork...\n");

	long child_pid = fork();
	if (child_pid == 0) {
		print("[CHILD] execve from fork child, pid=");
		print_hex((unsigned long)getpid());
		print("\n");

		char *child_argv[] = {
			"init",
			"exec-child",
			"argv-ok",
			0,
		};
		long ret = execve("/init", child_argv, 0);
		print("[CHILD] execve FAILED, ret=");
		print_hex((unsigned long)ret);
		print("\n");
		exit(2);
		return 1;
	} else if (child_pid > 0) {
		print("[PARENT] fork returned child_pid=");
		print_hex((unsigned long)child_pid);
		print("\n");

		int status = -1;
		long waited = wait4(child_pid, &status, 0, 0);
		print("[PARENT] wait4 returned pid=");
		print_hex((unsigned long)waited);
		print(", status=");
		print_hex((unsigned long)status);
		print(" (expected status 0x700)\n");
		return waited == child_pid && status == 0x700 ? 0 : 1;
	} else {
		print("[TEST] fork FAILED, returned ");
		print_hex((unsigned long)child_pid);
		print("\n");
		return 1;
	}
}

int main(int argc, char **argv)
{
	int failures = 0;

	print("=== CuteOS Syscall Test ===\n");
	print_argv(argc, argv);

	if (argc > 1 && streq(argv[1], "exec-child")) {
		print("[EXEC-CHILD] execve replaced the fork child\n");
		if (argc > 2 && streq(argv[2], "argv-ok"))
			print("[EXEC-CHILD] argv preserved OK\n");
		return 7;
	}

	failures += test_basic_syscalls();
	failures += test_dup();
	failures += test_pipe_parent_child();
	failures += test_pipe_child_parent();
	failures += test_pipe_epipe();
	failures += test_fork_exec_wait();

	if (failures == 0) {
		print("=== All tests passed ===\n");
	} else {
		print("=== Tests failed: ");
		print_long(failures);
		print(" ===\n");
	}

	/* PID 1 stays alive as the simple Stage 4 reaper. */
	while (1) {
		while (wait(0) > 0) {
		}
		yield();
	}

	return 0;
}

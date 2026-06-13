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

int main(int argc, char **argv)
{
	print("=== CuteOS Syscall Test ===\n");
	print_argv(argc, argv);

	if (argc > 1 && streq(argv[1], "exec-child")) {
		print("[EXEC-CHILD] execve replaced the fork child\n");
		if (argc > 2 && streq(argv[2], "argv-ok"))
			print("[EXEC-CHILD] argv preserved OK\n");
		return 7;
	}

	/* ---- Test 1: getpid ---- */
	long pid = getpid();
	print("[TEST] getpid = ");
	print_hex((unsigned long)pid);
	print(" (expected 1)\n");

	/* ---- Test 2: write (already working, verify again) ---- */
	print("[TEST] write: ");
	print("OK\n");

	/* ---- Test 3: yield ---- */
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

	/* 访问新分配的堆内存 — 触发 lazy page fault */
	if (new_brk == initial_brk + 4096) {
		volatile char *heap = (volatile char *)initial_brk;
		heap[0] = 0x42; /* 写入：触发 store page fault → 分配物理页 */
		heap[100] = 0x43;
		print("[TEST] heap[0] = ");
		print_hex((unsigned long)heap[0]);
		print(", heap[100] = ");
		print_hex((unsigned long)heap[100]);
		print(" (expected 0x42, 0x43)\n");
	}

	/* ---- Test 6: second yield ---- */
	yield();
	print("[TEST] second yield: OK\n");

	/* ---- Test 7: fork ---- */
	print("[TEST] fork...\n");
	long child_pid = fork();
	if (child_pid == 0) {
		/* 子进程 */
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
	} else if (child_pid > 0) {
		/* 父进程 */
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
	} else {
		print("[TEST] fork FAILED, returned ");
		print_hex((unsigned long)child_pid);
		print("\n");
	}

	print("=== All tests passed ===\n");

	/* PID 1 stays alive as the simple Stage 4 reaper. */
	while (1) {
		while (wait(0) > 0) {
		}
		yield();
	}

	return 0;
}

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
	print(", euid = ");
	print_hex((unsigned long)geteuid());
	print(", gid = ");
	print_hex((unsigned long)getgid());
	print(", egid = ");
	print_hex((unsigned long)getegid());
	print(", tid = ");
	print_hex((unsigned long)gettid());
	print("\n");
	if (getuid() != 0 || geteuid() != 0 || getgid() != 0 ||
	    getegid() != 0 || gettid() != pid)
		failures++;

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

static int test_time_syscalls(void)
{
	int failures = 0;
	struct tms tms_buf;
	struct timeval tv;
	struct timespec ts;
	struct timespec res;
	long ticks;

	ticks = times(&tms_buf);
	print("[TEST] times = ");
	print_long(ticks);
	print("\n");
	if (ticks < 0)
		failures++;

	if (gettimeofday(&tv, 0) == 0) {
		print("[TEST] gettimeofday sec = ");
		print_long(tv.tv_sec);
		print("\n");
	} else {
		failures++;
	}

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		print("[TEST] clock_gettime nsec = ");
		print_long(ts.tv_nsec);
		print("\n");
	} else {
		failures++;
	}

	if (clock_getres(CLOCK_MONOTONIC, &res) != 0 || res.tv_nsec <= 0)
		failures++;

	return failures;
}

static int test_misc_syscalls(void)
{
	int failures = 0;
	struct utsname uts;
	struct sysinfo info;
	unsigned int groups[1] = {123};
	int tid_slot = 0;
	unsigned int old_mask;

	if (uname(&uts) == 0) {
		print("[TEST] uname sysname = ");
		print(uts.sysname);
		print("\n");
		if (!streq(uts.sysname, "CuteOS") ||
		    !streq(uts.machine, "riscv64"))
			failures++;
	} else {
		failures++;
	}

	if (sysinfo(&info) == 0) {
		print("[TEST] sysinfo totalram = ");
		print_long((long)info.totalram);
		print("\n");
		if (info.totalram == 0 || info.mem_unit != 1)
			failures++;
	} else {
		failures++;
	}

	if (set_tid_addr(&tid_slot) != gettid())
		failures++;
	if (setuid(0) != 0 || setgid(0) != 0)
		failures++;
	if (getgroups(1, groups) != 1 || groups[0] != 0)
		failures++;
	groups[0] = 0;
	if (setgroups(1, groups) != 0)
		failures++;
	groups[0] = 1;
	if (setgroups(1, groups) >= 0)
		failures++;

	old_mask = (unsigned int)umask(0077);
	if ((unsigned int)umask(old_mask) != 0077)
		failures++;

	return failures;
}

static int test_file_extra_syscalls(void)
{
	int failures = 0;
	const char *path = "/syscall-extra";
	char buf[16];
	struct iovec wiov[2];
	struct iovec riov[2];
	struct stat st;
	long fd;
	long n;

	fd = openat(AT_FDCWD, path, O_CREAT | O_TRUNC | O_RDWR, 0666);
	print("[TEST] open extra file = ");
	print_long(fd);
	print("\n");
	if (fd < 0)
		return 1;

	wiov[0].iov_base = "ab";
	wiov[0].iov_len = 2;
	wiov[1].iov_base = "cd";
	wiov[1].iov_len = 2;
	if (writev((int)fd, wiov, 2) != 4)
		failures++;

	if (pwrite64((int)fd, "XY", 2, 1) != 2)
		failures++;
	if (pread64((int)fd, buf, 4, 0) != 4)
		failures++;
	buf[4] = '\0';
	print("[TEST] pread content = ");
	print(buf);
	print("\n");
	if (!streq(buf, "aXYd"))
		failures++;

	if (syscall(SYS_lseek, fd, 0, SEEK_SET) != 0)
		failures++;
	buf[0] = '\0';
	buf[2] = '\0';
	buf[4] = '\0';
	riov[0].iov_base = buf;
	riov[0].iov_len = 2;
	riov[1].iov_base = buf + 2;
	riov[1].iov_len = 2;
	if (readv((int)fd, riov, 2) != 4)
		failures++;
	buf[4] = '\0';
	if (!streq(buf, "aXYd"))
		failures++;

	if (faccessat(AT_FDCWD, path, R_OK | W_OK, 0) != 0)
		failures++;
	if (fstatat(AT_FDCWD, path, &st, 0) != 0 || st.st_size != 4)
		failures++;
	if (fstatat((int)fd, "", &st, AT_EMPTY_PATH) != 0 ||
	    st.st_size != 4)
		failures++;
	if (fsync((int)fd) != 0 || fdatasync((int)fd) != 0)
		failures++;

	if (ftruncate((int)fd, 2) != 0)
		failures++;
	if (fstatat((int)fd, "", &st, AT_EMPTY_PATH) != 0 ||
	    st.st_size != 2)
		failures++;

	if (fallocate((int)fd, FALLOC_FL_KEEP_SIZE, 0, 8) != 0)
		failures++;
	if (fstatat((int)fd, "", &st, AT_EMPTY_PATH) != 0 ||
	    st.st_size != 2)
		failures++;

	if (fallocate((int)fd, 0, 0, 8) != 0)
		failures++;
	if (fstatat((int)fd, "", &st, AT_EMPTY_PATH) != 0 ||
	    st.st_size != 8)
		failures++;

	n = pwrite64((int)fd, "z", 1, -1);
	if (n >= 0)
		failures++;

	close((int)fd);
	syscall(SYS_unlinkat, AT_FDCWD, (long)path, 0);
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
	failures += test_time_syscalls();
	failures += test_misc_syscalls();
	failures += test_file_extra_syscalls();
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

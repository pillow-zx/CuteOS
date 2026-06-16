#include <ulib.h>

static void print_argv_envp(int argc, char **argv, char **envp)
{
	printf("%s", "[TEST] argc = ");
	printf("0x%lx", (unsigned long)((unsigned long)argc));
	printf("%s", "\n");

	for (int i = 0; i < argc; i++) {
		printf("%s", "[TEST] argv[");
		printf("0x%lx", (unsigned long)((unsigned long)i));
		printf("%s", "] = ");
		printf("%s", argv[i]);
		printf("%s", "\n");
	}

	for (int i = 0; envp && envp[i] != NULL; i++) {
		printf("%s", "[TEST] envp[");
		printf("0x%lx", (unsigned long)((unsigned long)i));
		printf("%s", "] = ");
		printf("%s", envp[i]);
		printf("%s", "\n");
	}
}

static int test_basic_syscalls(void)
{
	int failures = 0;
	long pid = getpid();

	printf("%s", "[TEST] getpid = ");
	printf("0x%lx", (unsigned long)((unsigned long)pid));
	printf("%s", "\n");
	if (pid <= 1)
		failures++;

	printf("%s", "[TEST] getppid = ");
	printf("0x%lx", (unsigned long)((unsigned long)getppid()));
	printf("%s", ", uid = ");
	printf("0x%lx", (unsigned long)((unsigned long)getuid()));
	printf("%s", ", euid = ");
	printf("0x%lx", (unsigned long)((unsigned long)geteuid()));
	printf("%s", ", gid = ");
	printf("0x%lx", (unsigned long)((unsigned long)getgid()));
	printf("%s", ", egid = ");
	printf("0x%lx", (unsigned long)((unsigned long)getegid()));
	printf("%s", ", tid = ");
	printf("0x%lx", (unsigned long)((unsigned long)gettid()));
	printf("%s", "\n");
	if (getuid() != 0 || geteuid() != 0 || getgid() != 0 ||
	    getegid() != 0 || gettid() != pid)
		failures++;

	printf("%s", "[TEST] write: OK\n");
	printf("%s", "[TEST] yield...\n");
	yield();
	printf("%s", "[TEST] yield: returned OK\n");

	long initial_brk = brk(0);
	long new_brk = brk(initial_brk + 4096);

	printf("%s", "[TEST] brk extend = ");
	printf("0x%lx", (unsigned long)((unsigned long)new_brk));
	printf("%s", "\n");
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

	printf("%s", "[TEST] mmap anonymous = ");
	printf("0x%lx", (unsigned long)((unsigned long)map));
	printf("%s", "\n");
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
	printf("%s", "[TEST] times = ");
	printf("%ld", (long)(ticks));
	printf("%s", "\n");
	if (ticks < 0)
		failures++;

	if (gettimeofday(&tv, 0) == 0) {
		printf("%s", "[TEST] gettimeofday sec = ");
		printf("%ld", (long)(tv.tv_sec));
		printf("%s", "\n");
	} else {
		failures++;
	}

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		printf("%s", "[TEST] clock_gettime nsec = ");
		printf("%ld", (long)(ts.tv_nsec));
		printf("%s", "\n");
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
		printf("%s", "[TEST] uname sysname = ");
		printf("%s", uts.sysname);
		printf("%s", "\n");
		if (!streq(uts.sysname, "CuteOS") ||
		    !streq(uts.machine, "riscv64"))
			failures++;
	} else {
		failures++;
	}

	if (sysinfo(&info) == 0) {
		printf("%s", "[TEST] sysinfo totalram = ");
		printf("%ld", (long)((long)info.totalram));
		printf("%s", "\n");
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
	printf("%s", "[TEST] open extra file = ");
	printf("%ld", (long)(fd));
	printf("%s", "\n");
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
	printf("%s", "[TEST] pread content = ");
	printf("%s", buf);
	printf("%s", "\n");
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

static int test_open_permissions(void)
{
	const char *path = "/root-only";
	long fd;
	long child;
	int status = -1;

	fd = openat(AT_FDCWD, path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (fd < 0)
		return 1;
	close((int)fd);

	child = fork();
	if (child == 0) {
		if (setgid(1000) != 0 || setuid(1000) != 0)
			exit(1);
		if (open(path, O_RDONLY) != -EACCES)
			exit(2);
		if (faccessat(AT_FDCWD, path, R_OK, 0) != -EACCES)
			exit(3);
		exit(0);
	}
	if (child < 0) {
		syscall(SYS_unlinkat, AT_FDCWD, (long)path, 0);
		return 1;
	}

	if (wait4(child, &status, 0, 0) != child)
		status = -1;
	syscall(SYS_unlinkat, AT_FDCWD, (long)path, 0);

	printf("%s", "[TEST] permission child status = ");
	printf("0x%lx", (unsigned long)((unsigned long)status));
	printf("%s", "\n");

	return status == 0 ? 0 : 1;
}

static int expect_readlink(const char *path, const char *target)
{
	char buf[128];
	long n;

	n = readlinkat(AT_FDCWD, path, buf, sizeof(buf) - 1);
	if (n != (long)strlen(target)) {
		printf("%s", "[TEST] readlink length failed: ");
		printf("%s", path);
		printf("%s", " ret=");
		printf("%ld", (long)(n));
		printf("%s", "\n");
		return 1;
	}
	buf[n] = '\0';
	if (!streq(buf, target)) {
		printf("%s", "[TEST] readlink target failed: ");
		printf("%s", path);
		printf("%s", " target=");
		printf("%s", buf);
		printf("%s", "\n");
		return 1;
	}
	return 0;
}

static int test_symlink_syscalls(void)
{
	int failures = 0;
	const char *fast = "/fast-syscall-test";
	const char *fast_target = "/bin/syscall-test";
	const char *slow = "/slow-syscall-test";
	const char *slow_target =
		"/slow-target-abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-bin";
	char tiny[4];
	struct stat st;
	long fd;
	long n;

	if (expect_readlink(fast, fast_target) != 0)
		failures++;
	if (expect_readlink(slow, slow_target) != 0)
		failures++;

	n = readlinkat(AT_FDCWD, fast, tiny, sizeof(tiny));
	if (n != (long)sizeof(tiny) || tiny[0] != '/' || tiny[1] != 'b' ||
	    tiny[2] != 'i' || tiny[3] != 'n') {
		printf("%s", "[TEST] readlink truncate failed ret=");
		printf("%ld", (long)(n));
		printf("%s", "\n");
		failures++;
	}

	if (fstatat(AT_FDCWD, fast, &st, 0) != 0 ||
	    (st.st_mode & S_IFMT) != S_IFREG) {
		printf("%s", "[TEST] fstat follow symlink failed mode=");
		printf("%ld", (long)(st.st_mode));
		printf("%s", "\n");
		failures++;
	}
	if (fstatat(AT_FDCWD, fast, &st, AT_SYMLINK_NOFOLLOW) != 0 ||
	    (st.st_mode & S_IFMT) != S_IFLNK) {
		printf("%s", "[TEST] fstat nofollow symlink failed mode=");
		printf("%ld", (long)(st.st_mode));
		printf("%s", "\n");
		failures++;
	}

	fd = open(fast, O_RDONLY);
	if (fd >= 0)
		close((int)fd);
	else {
		printf("%s", "[TEST] open symlink read failed ret=");
		printf("%ld", (long)(fd));
		printf("%s", "\n");
		failures++;
	}

	fd = open(fast, O_WRONLY);
	if (fd >= 0)
		close((int)fd);
	else {
		printf("%s", "[TEST] open symlink write failed ret=");
		printf("%ld", (long)(fd));
		printf("%s", "\n");
		failures++;
	}

	fd = open("/loop-symlink", O_RDONLY);
	if (fd != -ELOOP) {
		printf("%s", "[TEST] open loop symlink failed ret=");
		printf("%ld", (long)(fd));
		printf("%s", "\n");
		if (fd >= 0)
			close((int)fd);
		failures++;
	}
	if (expect_readlink("/loop-symlink", "/loop-symlink") != 0)
		failures++;

	return failures;
}

static int test_dev_null(void)
{
	long fd = open("/dev/null", O_WRONLY);
	long n;

	printf("%s", "[TEST] open /dev/null = ");
	printf("%ld", (long)(fd));
	printf("%s", "\n");
	if (fd < 0)
		return 1;

	n = write((int)fd, "discard", 7);
	close((int)fd);
	return n == 7 ? 0 : 1;
}

static int test_pipe(void)
{
	int pipefd[2];

	printf("%s", "[TEST] pipe parent->child...\n");
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

	printf("%s", "[TEST] pipe child status = ");
	printf("0x%lx", (unsigned long)((unsigned long)status));
	printf("%s", "\n");

	return waited == child && status == 0xb00 ? 0 : 1;
}

static int test_fork_exec_wait(void)
{
	printf("%s", "[TEST] fork/exec/wait...\n");

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

		printf("%s", "[CHILD] execve FAILED, ret=");
		printf("%ld", (long)(ret));
		printf("%s", "\n");
		exit(2);
	}
	if (child < 0)
		return 1;

	int status = -1;
	long waited = wait4(child, &status, 0, 0);

	printf("%s", "[TEST] exec child status = ");
	printf("0x%lx", (unsigned long)((unsigned long)status));
	printf("%s", "\n");
	return waited == child && status == 0x700 ? 0 : 1;
}

int main(int argc, char **argv, char **envp)
{
	int failures = 0;

	if (argc > 1 && streq(argv[1], "exec-child")) {
		printf("%s", "[EXEC-CHILD] execve replaced the fork child\n");
		if (argc > 2 && streq(argv[2], "argv-ok") && envp && envp[0] &&
		    streq(envp[0], "CUTEOS_ENV=ok"))
			return 7;
		return 8;
	}

	printf("%s", "=== CuteOS Syscall Test ===\n");
	print_argv_envp(argc, argv, envp);

	failures += test_basic_syscalls();
	failures += test_dup();
	failures += test_time_syscalls();
	failures += test_misc_syscalls();
	failures += test_file_extra_syscalls();
	failures += test_open_permissions();
	failures += test_symlink_syscalls();
	failures += test_dev_null();
	failures += test_pipe();
	failures += test_fork_exec_wait();

	if (failures == 0) {
		printf("%s", "=== All tests passed ===\n");
	} else {
		printf("%s", "=== Tests failed: ");
		printf("%ld", (long)(failures));
		printf("%s", " ===\n");
	}

	return failures == 0 ? 0 : 1;
}

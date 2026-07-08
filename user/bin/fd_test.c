/*
 * user/bin/fd_test.c - file descriptor table and fcntl tests
 */

#include <ulib.h>

#define CUTEOS_NR_OPEN	  32
#define TEST_PIPE_SIZE	  4096
#define TEST_F_GETLK	  5
#define TEST_F_SETOWN	  8
#define TEST_F_SETPIPE_SZ 1031
#define TEST_F_GETPIPE_SZ 1032

static int fd_flags_expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int test_get_set_fd_flags(void)
{
	long fd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/fcntl_fd_flags", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open flags test: %ld\n", fd);
		return 1;
	}

	failed += fd_flags_expect_ret("initial F_GETFD",
				      fcntl((int)fd, F_GETFD, 0), 0);
	failed += fd_flags_expect_ret("F_SETFD FD_CLOEXEC",
				      fcntl((int)fd, F_SETFD, FD_CLOEXEC), 0);
	failed += fd_flags_expect_ret("F_GETFD after set",
				      fcntl((int)fd, F_GETFD, 0), FD_CLOEXEC);
	failed += fd_flags_expect_ret(
		"F_SETFD extra bits",
		fcntl((int)fd, F_SETFD, FD_CLOEXEC | 0x100), 0);
	failed += fd_flags_expect_ret("F_GETFD after extra bits",
				      fcntl((int)fd, F_GETFD, 0), FD_CLOEXEC);
	failed += fd_flags_expect_ret("F_SETFD clear",
				      fcntl((int)fd, F_SETFD, 0), 0);
	failed += fd_flags_expect_ret("F_GETFD after clear",
				      fcntl((int)fd, F_GETFD, 0), 0);

	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_fd_flags", 0);
	return failed;
}

static int test_fd_flags_error_paths(void)
{
	long fd;
	int failed = 0;

	failed += fd_flags_expect_ret("F_GETFD invalid fd",
				      fcntl(-1, F_GETFD, 0), -EBADF);
	failed += fd_flags_expect_ret("F_SETFD invalid fd",
				      fcntl(-1, F_SETFD, FD_CLOEXEC), -EBADF);

	fd = openat(AT_FDCWD, "/fcntl_fd_unsupported",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open unsupported cmd test: %ld\n", fd);
		return failed + 1;
	}

	failed += fd_flags_expect_ret("unsupported fcntl cmd",
				      fcntl((int)fd, 999, 0), -EINVAL);
	failed += fd_flags_expect_ret("unsupported lock fcntl cmd",
				      fcntl((int)fd, TEST_F_GETLK, 0), -EINVAL);
	failed +=
		fd_flags_expect_ret("unsupported owner fcntl cmd",
				    fcntl((int)fd, TEST_F_SETOWN, 0), -EINVAL);
	failed += fd_flags_expect_ret("unsupported pipe set size cmd",
				      fcntl((int)fd, TEST_F_SETPIPE_SZ, 4096),
				      -EINVAL);
	failed += fd_flags_expect_ret("unsupported pipe get size cmd",
				      fcntl((int)fd, TEST_F_GETPIPE_SZ, 0),
				      -EINVAL);
	failed += fd_flags_expect_ret("unknown fcntl invalid fd",
				      fcntl(-1, 999, 0), -EBADF);
	failed += fd_flags_expect_ret("unsupported fcntl invalid fd",
				      fcntl(-1, TEST_F_GETLK, 0), -EBADF);

	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_fd_unsupported", 0);
	return failed;
}

static int test_reused_fd_clears_cloexec(void)
{
	long fd;
	long next;
	int failed = 0;

	fd = openat(AT_FDCWD, "/fcntl_fd_reuse_a", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open reuse source: %ld\n", fd);
		return 1;
	}

	failed += fd_flags_expect_ret("set cloexec before close",
				      fcntl((int)fd, F_SETFD, FD_CLOEXEC), 0);
	close((int)fd);

	next = openat(AT_FDCWD, "/fcntl_fd_reuse_b", O_CREAT | O_RDWR | O_TRUNC,
		      0644);
	if (next < 0) {
		printf("FAIL: open reuse target: %ld\n", next);
		unlinkat(AT_FDCWD, "/fcntl_fd_reuse_a", 0);
		return failed + 1;
	}
	if (next != fd)
		printf("WARN: reuse test got fd %ld after closing %ld\n", next,
		       fd);
	failed += fd_flags_expect_ret("reused fd F_GETFD",
				      fcntl((int)next, F_GETFD, 0), 0);

	close((int)next);
	unlinkat(AT_FDCWD, "/fcntl_fd_reuse_a", 0);
	unlinkat(AT_FDCWD, "/fcntl_fd_reuse_b", 0);
	return failed;
}

static int test_exec_closes_cloexec_fd(void)
{
	char fd_buf[16];
	char *argv[] = {"fd_test", "--check-cloexec", fd_buf, 0};
	char *envp[] = {"PATH=/bin", 0};
	int status = 0;
	long fd;
	long pid;
	long waited;

	fd = openat(AT_FDCWD, "/fcntl_fd_exec", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open exec test: %ld\n", fd);
		return 1;
	}
	if (fcntl((int)fd, F_SETFD, FD_CLOEXEC) != 0) {
		printf("FAIL: set cloexec for exec test\n");
		close((int)fd);
		unlinkat(AT_FDCWD, "/fcntl_fd_exec", 0);
		return 1;
	}

	snprintf(fd_buf, sizeof(fd_buf), "%ld", fd);
	pid = fork();
	if (pid < 0) {
		printf("FAIL: fork exec test: %ld\n", pid);
		close((int)fd);
		unlinkat(AT_FDCWD, "/fcntl_fd_exec", 0);
		return 1;
	}
	if (pid == 0) {
		execve("/bin/fd_test", argv, envp);
		exit(3);
	}

	waited = wait4(pid, &status, 0, NULL);
	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_fd_exec", 0);
	if (waited != pid) {
		printf("FAIL: wait exec checker expected %ld got %ld\n", pid,
		       waited);
		return 1;
	}
	if (status != 0) {
		printf("FAIL: exec checker status expected 0 got %d\n", status);
		return 1;
	}

	return 0;
}

static int test_openat_cloexec(void)
{
	char fd_buf[16];
	char *argv[] = {"fd_test", "--check-cloexec", fd_buf, 0};
	char *envp[] = {"PATH=/bin", 0};
	int status = 0;
	long fd;
	long pid;
	long waited;

	fd = openat(AT_FDCWD, "/fcntl_fd_open_cloexec",
		    O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0) {
		printf("FAIL: openat cloexec test: %ld\n", fd);
		return 1;
	}
	if (fcntl((int)fd, F_GETFD, 0) != FD_CLOEXEC) {
		printf("FAIL: openat O_CLOEXEC not visible through F_GETFD\n");
		close((int)fd);
		unlinkat(AT_FDCWD, "/fcntl_fd_open_cloexec", 0);
		return 1;
	}

	snprintf(fd_buf, sizeof(fd_buf), "%ld", fd);
	pid = fork();
	if (pid < 0) {
		printf("FAIL: fork openat cloexec test: %ld\n", pid);
		close((int)fd);
		unlinkat(AT_FDCWD, "/fcntl_fd_open_cloexec", 0);
		return 1;
	}
	if (pid == 0) {
		execve("/bin/fd_test", argv, envp);
		exit(3);
	}

	waited = wait4(pid, &status, 0, NULL);
	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_fd_open_cloexec", 0);
	if (waited != pid) {
		printf("FAIL: wait openat cloexec expected %ld got %ld\n", pid,
		       waited);
		return 1;
	}
	if (status != 0) {
		printf("FAIL: openat cloexec checker status got %d\n", status);
		return 1;
	}

	return 0;
}

static int file_status_expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int file_status_expect_mask(const char *name, long flags, long mask,
				   long want)
{
	return file_status_expect_ret(name, flags & mask, want);
}

static int test_getfl_filters_open_flags(void)
{
	long fd;
	long dirfd;
	long flags;
	int failed = 0;

	fd = openat(AT_FDCWD, "/fcntl_fl_get",
		    O_CREAT | O_RDWR | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
	if (fd < 0) {
		printf("FAIL: open getfl file: %ld\n", fd);
		return 1;
	}

	flags = fcntl((int)fd, F_GETFL, 0);
	if (flags < 0) {
		printf("FAIL: F_GETFL file returned %ld\n", flags);
		failed++;
	} else {
		failed += file_status_expect_mask("F_GETFL access mode", flags,
						  O_RDWR, O_RDWR);
		failed += file_status_expect_mask("F_GETFL append", flags,
						  O_APPEND, O_APPEND);
		failed += file_status_expect_mask("F_GETFL omits create", flags,
						  O_CREAT, 0);
		failed += file_status_expect_mask("F_GETFL omits trunc", flags,
						  O_TRUNC, 0);
		failed += file_status_expect_mask("F_GETFL omits cloexec",
						  flags, O_CLOEXEC, 0);
	}

	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_fl_get", 0);

	dirfd = openat(AT_FDCWD, "/", O_RDONLY | O_DIRECTORY, 0);
	if (dirfd < 0) {
		printf("FAIL: open getfl dir: %ld\n", dirfd);
		return failed + 1;
	}

	flags = fcntl((int)dirfd, F_GETFL, 0);
	if (flags < 0) {
		printf("FAIL: F_GETFL dir returned %ld\n", flags);
		failed++;
	} else {
		failed += file_status_expect_mask("F_GETFL dir access mode",
						  flags, O_RDWR, O_RDONLY);
		failed += file_status_expect_mask("F_GETFL directory", flags,
						  O_DIRECTORY, O_DIRECTORY);
	}
	close((int)dirfd);

	return failed;
}

static int test_setfl_append_behavior(void)
{
	char buf[8];
	long fd;
	long n;
	int failed = 0;

	fd = openat(AT_FDCWD, "/fcntl_fl_append", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open append behavior: %ld\n", fd);
		return 1;
	}

	write((int)fd, "abc", 3);
	lseek((int)fd, 0, SEEK_SET);
	failed += file_status_expect_ret("F_SETFL O_APPEND",
					 fcntl((int)fd, F_SETFL, O_APPEND), 0);
	failed += file_status_expect_mask("F_GETFL after set append",
					  fcntl((int)fd, F_GETFL, 0), O_APPEND,
					  O_APPEND);
	write((int)fd, "Z", 1);
	lseek((int)fd, 0, SEEK_SET);
	n = read((int)fd, buf, sizeof(buf));
	if (n != 4 || buf[0] != 'a' || buf[1] != 'b' || buf[2] != 'c' ||
	    buf[3] != 'Z') {
		printf("FAIL: append write content n=%ld\n", n);
		failed++;
	}

	failed += file_status_expect_ret("F_SETFL clear append",
					 fcntl((int)fd, F_SETFL, 0), 0);
	failed += file_status_expect_mask("F_GETFL after clear append",
					  fcntl((int)fd, F_GETFL, 0), O_APPEND,
					  0);
	lseek((int)fd, 0, SEEK_SET);
	write((int)fd, "Y", 1);
	lseek((int)fd, 0, SEEK_SET);
	n = read((int)fd, buf, sizeof(buf));
	if (n < 1 || buf[0] != 'Y') {
		printf("FAIL: non-append write content n=%ld\n", n);
		failed++;
	}

	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_fl_append", 0);
	return failed;
}

static int test_setfl_shared_by_dup(void)
{
	long fd;
	long dupfd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/fcntl_fl_dup", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open dup sharing: %ld\n", fd);
		return 1;
	}
	dupfd = dup((int)fd);
	if (dupfd < 0) {
		printf("FAIL: dup sharing: %ld\n", dupfd);
		close((int)fd);
		unlinkat(AT_FDCWD, "/fcntl_fl_dup", 0);
		return 1;
	}

	failed +=
		file_status_expect_ret("dup F_SETFL O_APPEND",
				       fcntl((int)dupfd, F_SETFL, O_APPEND), 0);
	failed += file_status_expect_mask("original sees dup append",
					  fcntl((int)fd, F_GETFL, 0), O_APPEND,
					  O_APPEND);
	failed += file_status_expect_ret("original clears append",
					 fcntl((int)fd, F_SETFL, 0), 0);
	failed += file_status_expect_mask("dup sees append clear",
					  fcntl((int)dupfd, F_GETFL, 0),
					  O_APPEND, 0);

	close((int)dupfd);
	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_fl_dup", 0);
	return failed;
}

static int test_file_status_error_paths(void)
{
	long fd;
	int failed = 0;

	failed += file_status_expect_ret("F_GETFL invalid fd",
					 fcntl(-1, F_GETFL, 0), -EBADF);
	failed += file_status_expect_ret("F_SETFL invalid fd",
					 fcntl(-1, F_SETFL, 0), -EBADF);

	fd = openat(AT_FDCWD, "/fcntl_fl_error", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open error paths: %ld\n", fd);
		return failed + 1;
	}

	failed += file_status_expect_ret(
		"F_SETFL ignores immutable bits",
		fcntl((int)fd, F_SETFL,
		      O_APPEND | O_CREAT | O_TRUNC | O_CLOEXEC),
		0);
	failed += file_status_expect_mask("immutable bits not reported",
					  fcntl((int)fd, F_GETFL, 0),
					  O_CREAT | O_TRUNC | O_CLOEXEC, 0);
	failed += file_status_expect_ret("F_SETFL sets O_NONBLOCK",
					 fcntl((int)fd, F_SETFL, O_NONBLOCK),
					 0);
	failed += file_status_expect_mask("F_GETFL after set nonblock",
					  fcntl((int)fd, F_GETFL, 0),
					  O_NONBLOCK, O_NONBLOCK);
	failed += file_status_expect_ret("F_SETFL rejects O_DSYNC",
					 fcntl((int)fd, F_SETFL, O_DSYNC),
					 -EINVAL);

	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_fl_error", 0);
	return failed;
}

static int test_pipe2_nonblock_cloexec(void)
{
	static char fill[TEST_PIPE_SIZE];
	char ch;
	int fds[2] = {-1, -1};
	long ret;
	int failed = 0;

	ret = syscall(SYS_pipe2, (long)fds, O_NONBLOCK | O_CLOEXEC);
	if (ret != 0) {
		printf("FAIL: pipe2 nonblock cloexec returned %ld\n", ret);
		return 1;
	}

	failed += file_status_expect_ret("pipe2 read cloexec",
					 fcntl(fds[0], F_GETFD, 0),
					 FD_CLOEXEC);
	failed += file_status_expect_ret("pipe2 write cloexec",
					 fcntl(fds[1], F_GETFD, 0),
					 FD_CLOEXEC);
	failed += file_status_expect_mask("pipe2 read nonblock",
					  fcntl(fds[0], F_GETFL, 0),
					  O_NONBLOCK, O_NONBLOCK);
	failed += file_status_expect_mask("pipe2 write nonblock",
					  fcntl(fds[1], F_GETFL, 0),
					  O_NONBLOCK, O_NONBLOCK);
	failed += file_status_expect_mask("pipe2 write access mode",
					  fcntl(fds[1], F_GETFL, 0),
					  O_ACCMODE, O_WRONLY);
	failed += file_status_expect_ret("pipe2 empty nonblock read",
					 read(fds[0], &ch, 1), -EAGAIN);

	memset(fill, 'x', sizeof(fill));
	ret = write(fds[1], fill, sizeof(fill));
	if (ret != (long)sizeof(fill)) {
		printf("FAIL: pipe2 fill expected %ld got %ld\n",
		       (long)sizeof(fill), ret);
		failed++;
	} else {
		failed += file_status_expect_ret("pipe2 full nonblock write",
						 write(fds[1], "y", 1),
						 -EAGAIN);
	}

	close(fds[0]);
	close(fds[1]);

	failed += file_status_expect_ret(
		"pipe2 rejects unknown flags",
		syscall(SYS_pipe2, (long)fds, O_NONBLOCK | O_DIRECTORY),
		-EINVAL);
	return failed;
}

static int test_setfl_nonblock_behavior(void)
{
	char ch;
	int fds[2] = {-1, -1};
	long ret;
	int failed = 0;

	ret = pipe(fds);
	if (ret != 0) {
		printf("FAIL: pipe setfl nonblock returned %ld\n", ret);
		return 1;
	}

	failed += file_status_expect_ret("F_SETFL pipe O_NONBLOCK",
					 fcntl(fds[0], F_SETFL, O_NONBLOCK),
					 0);
	failed += file_status_expect_mask("F_GETFL pipe nonblock",
					  fcntl(fds[0], F_GETFL, 0),
					  O_NONBLOCK, O_NONBLOCK);
	failed += file_status_expect_ret("F_SETFL nonblock read EAGAIN",
					 read(fds[0], &ch, 1), -EAGAIN);
	failed += file_status_expect_ret("F_SETFL pipe clear nonblock",
					 fcntl(fds[0], F_SETFL, 0), 0);
	failed += file_status_expect_mask("F_GETFL pipe nonblock clear",
					  fcntl(fds[0], F_GETFL, 0),
					  O_NONBLOCK, 0);

	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int dupfd_expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int dupfd_expect_mask(const char *name, long flags, long mask, long want)
{
	return dupfd_expect_ret(name, flags & mask, want);
}

static int test_dupfd_minimum_and_cloexec(void)
{
	long fd;
	long hold;
	long dupfd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/fcntl_dupfd_min", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open dupfd min: %ld\n", fd);
		return 1;
	}

	hold = openat(AT_FDCWD, "/fcntl_dupfd_hold", O_CREAT | O_RDWR | O_TRUNC,
		      0644);
	if (hold < 0) {
		printf("FAIL: open dupfd hold: %ld\n", hold);
		close((int)fd);
		unlinkat(AT_FDCWD, "/fcntl_dupfd_min", 0);
		return 1;
	}

	failed += dupfd_expect_ret("set source cloexec",
				   fcntl((int)fd, F_SETFD, FD_CLOEXEC), 0);
	dupfd = fcntl((int)fd, F_DUPFD, hold);
	if (dupfd < 0) {
		printf("FAIL: F_DUPFD min returned %ld\n", dupfd);
		failed++;
	} else {
		failed += dupfd_expect_ret("F_DUPFD lowest free", dupfd,
					   hold + 1);
		failed += dupfd_expect_ret("F_DUPFD clears cloexec",
					   fcntl((int)dupfd, F_GETFD, 0), 0);
		close((int)dupfd);
	}

	close((int)hold);
	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_dupfd_min", 0);
	unlinkat(AT_FDCWD, "/fcntl_dupfd_hold", 0);
	return failed;
}

static int test_dupfd_cloexec_exec(void)
{
	char fd_buf[16];
	char *argv[] = {"fd_test", "--check-cloexec", fd_buf, 0};
	char *envp[] = {"PATH=/bin", 0};
	long fd;
	long dupfd;
	long pid;
	long waited;
	int status = 0;
	int failed = 0;

	fd = openat(AT_FDCWD, "/fcntl_dupfd_cloexec",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open dupfd cloexec: %ld\n", fd);
		return 1;
	}

	dupfd = fcntl((int)fd, F_DUPFD_CLOEXEC, 10);
	if (dupfd < 0) {
		printf("FAIL: F_DUPFD_CLOEXEC returned %ld\n", dupfd);
		close((int)fd);
		unlinkat(AT_FDCWD, "/fcntl_dupfd_cloexec", 0);
		return 1;
	}

	failed += dupfd_expect_ret("F_DUPFD_CLOEXEC minimum", dupfd, 10);
	failed += dupfd_expect_ret("F_DUPFD_CLOEXEC flag",
				   fcntl((int)dupfd, F_GETFD, 0), FD_CLOEXEC);

	snprintf(fd_buf, sizeof(fd_buf), "%ld", dupfd);
	pid = fork();
	if (pid < 0) {
		printf("FAIL: fork dupfd cloexec: %ld\n", pid);
		close((int)dupfd);
		close((int)fd);
		unlinkat(AT_FDCWD, "/fcntl_dupfd_cloexec", 0);
		return failed + 1;
	}
	if (pid == 0) {
		execve("/bin/fd_test", argv, envp);
		exit(3);
	}

	waited = wait4(pid, &status, 0, NULL);
	if (waited != pid) {
		printf("FAIL: wait dupfd cloexec expected %ld got %ld\n", pid,
		       waited);
		failed++;
	} else if (status != 0) {
		printf("FAIL: dupfd cloexec checker status got %d\n", status);
		failed++;
	}

	close((int)dupfd);
	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_dupfd_cloexec", 0);
	return failed;
}

static int test_dupfd_shares_status_flags(void)
{
	long fd;
	long dupfd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/fcntl_dupfd_flags", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open dupfd flags: %ld\n", fd);
		return 1;
	}

	dupfd = fcntl((int)fd, F_DUPFD, 0);
	if (dupfd < 0) {
		printf("FAIL: F_DUPFD flags returned %ld\n", dupfd);
		close((int)fd);
		unlinkat(AT_FDCWD, "/fcntl_dupfd_flags", 0);
		return 1;
	}

	failed += dupfd_expect_ret("dupfd F_SETFL O_APPEND",
				   fcntl((int)dupfd, F_SETFL, O_APPEND), 0);
	failed += dupfd_expect_mask("source sees dupfd append",
				    fcntl((int)fd, F_GETFL, 0), O_APPEND,
				    O_APPEND);
	failed += dupfd_expect_ret("source clears append",
				   fcntl((int)fd, F_SETFL, 0), 0);
	failed += dupfd_expect_mask("dupfd sees append clear",
				    fcntl((int)dupfd, F_GETFL, 0), O_APPEND, 0);

	close((int)dupfd);
	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_dupfd_flags", 0);
	return failed;
}

static int test_dupfd_error_paths(void)
{
	long fd;
	long fds[CUTEOS_NR_OPEN];
	int n = 0;
	int failed = 0;

	fd = openat(AT_FDCWD, "/fcntl_dupfd_error", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open dupfd error: %ld\n", fd);
		return 1;
	}

	failed +=
		dupfd_expect_ret("F_DUPFD invalid oldfd",
				 fcntl(-1, F_DUPFD, (unsigned long)-1), -EBADF);
	failed += dupfd_expect_ret("F_DUPFD negative min",
				   fcntl((int)fd, F_DUPFD, (unsigned long)-1),
				   -EINVAL);
	failed += dupfd_expect_ret("F_DUPFD min too high",
				   fcntl((int)fd, F_DUPFD, CUTEOS_NR_OPEN),
				   -EINVAL);
	failed += dupfd_expect_ret(
		"F_DUPFD_CLOEXEC negative min",
		fcntl((int)fd, F_DUPFD_CLOEXEC, (unsigned long)-1), -EINVAL);

	while (n < CUTEOS_NR_OPEN) {
		long next = openat(AT_FDCWD, "/fcntl_dupfd_fill",
				   O_CREAT | O_RDWR, 0644);
		if (next < 0)
			break;
		fds[n++] = next;
	}

	failed += dupfd_expect_ret("F_DUPFD table full",
				   fcntl((int)fd, F_DUPFD, 0), -EMFILE);

	while (n > 0)
		close((int)fds[--n]);
	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_dupfd_error", 0);
	unlinkat(AT_FDCWD, "/fcntl_dupfd_fill", 0);
	return failed;
}

static int check_cloexec_fd(int argc, char **argv)
{
	int fd;
	long ret;

	if (argc != 3) {
		printf("fd_test: expected fd argument\n");
		return 2;
	}

	fd = atoi(argv[2]);
	ret = fcntl(fd, F_GETFD, 0);
	if (ret != -EBADF) {
		printf("fd_test: fd %d expected -EBADF got %ld\n", fd, ret);
		return 1;
	}

	return 0;
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("fd_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc > 1 && streq(argv[1], "--check-cloexec"))
		return check_cloexec_fd(argc, argv);

	report_group("get/set fd flags", test_get_set_fd_flags(), &failed);
	report_group("fd flag error paths", test_fd_flags_error_paths(),
		     &failed);
	report_group("fd reuse clears cloexec", test_reused_fd_clears_cloexec(),
		     &failed);
	report_group("exec closes cloexec fd", test_exec_closes_cloexec_fd(),
		     &failed);
	report_group("openat O_CLOEXEC closes on exec", test_openat_cloexec(),
		     &failed);
	report_group("getfl filters open flags",
		     test_getfl_filters_open_flags(), &failed);
	report_group("setfl append behavior", test_setfl_append_behavior(),
		     &failed);
	report_group("setfl shared by dup", test_setfl_shared_by_dup(),
		     &failed);
	report_group("file status flag error paths",
		     test_file_status_error_paths(), &failed);
	report_group("pipe2 nonblock cloexec", test_pipe2_nonblock_cloexec(),
		     &failed);
	report_group("setfl nonblock behavior", test_setfl_nonblock_behavior(),
		     &failed);
	report_group("dupfd minimum and cloexec reset",
		     test_dupfd_minimum_and_cloexec(), &failed);
	report_group("dupfd cloexec closes on exec", test_dupfd_cloexec_exec(),
		     &failed);
	report_group("dupfd status flags shared",
		     test_dupfd_shares_status_flags(), &failed);
	report_group("dupfd error paths", test_dupfd_error_paths(), &failed);

	if (failed)
		printf("fd_test: %d test group(s) FAILED\n", failed);
	else
		printf("fd_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

/*
 * user/bin/fcntl_fd_test.c - fcntl F_GETFD/F_SETFD ABI tests
 */

#include <ulib.h>

static int expect_ret(const char *name, long got, long want)
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

	failed += expect_ret("initial F_GETFD", fcntl((int)fd, F_GETFD, 0), 0);
	failed += expect_ret("F_SETFD FD_CLOEXEC",
			     fcntl((int)fd, F_SETFD, FD_CLOEXEC), 0);
	failed += expect_ret("F_GETFD after set", fcntl((int)fd, F_GETFD, 0),
			     FD_CLOEXEC);
	failed += expect_ret("F_SETFD extra bits",
			     fcntl((int)fd, F_SETFD, FD_CLOEXEC | 0x100), 0);
	failed += expect_ret("F_GETFD after extra bits",
			     fcntl((int)fd, F_GETFD, 0), FD_CLOEXEC);
	failed += expect_ret("F_SETFD clear", fcntl((int)fd, F_SETFD, 0), 0);
	failed += expect_ret("F_GETFD after clear", fcntl((int)fd, F_GETFD, 0),
			     0);

	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_fd_flags", 0);
	return failed;
}

static int test_error_paths(void)
{
	long fd;
	int failed = 0;

	failed +=
		expect_ret("F_GETFD invalid fd", fcntl(-1, F_GETFD, 0), -EBADF);
	failed += expect_ret("F_SETFD invalid fd",
			     fcntl(-1, F_SETFD, FD_CLOEXEC), -EBADF);

	fd = openat(AT_FDCWD, "/fcntl_fd_unsupported",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open unsupported cmd test: %ld\n", fd);
		return failed + 1;
	}

	failed += expect_ret("unsupported fcntl cmd", fcntl((int)fd, 999, 0),
			     -EINVAL);

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

	failed += expect_ret("set cloexec before close",
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
	failed += expect_ret("reused fd F_GETFD", fcntl((int)next, F_GETFD, 0),
			     0);

	close((int)next);
	unlinkat(AT_FDCWD, "/fcntl_fd_reuse_a", 0);
	unlinkat(AT_FDCWD, "/fcntl_fd_reuse_b", 0);
	return failed;
}

static int test_exec_closes_cloexec_fd(void)
{
	char fd_buf[16];
	char *argv[] = {"fcntl_fd_checker", fd_buf, 0};
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
		execve("/bin/fcntl_fd_checker", argv, envp);
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
	char *argv[] = {"fcntl_fd_checker", fd_buf, 0};
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
		execve("/bin/fcntl_fd_checker", argv, envp);
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

int main(void)
{
	int failed = 0;

	printf("fcntl_fd_test: get/set fd flags ... ");
	if (test_get_set_fd_flags())
		failed++;
	else
		printf("PASS\n");

	printf("fcntl_fd_test: error paths ... ");
	if (test_error_paths())
		failed++;
	else
		printf("PASS\n");

	printf("fcntl_fd_test: fd reuse clears cloexec ... ");
	if (test_reused_fd_clears_cloexec())
		failed++;
	else
		printf("PASS\n");

	printf("fcntl_fd_test: exec closes cloexec fd ... ");
	if (test_exec_closes_cloexec_fd())
		failed++;
	else
		printf("PASS\n");

	printf("fcntl_fd_test: openat O_CLOEXEC closes on exec ... ");
	if (test_openat_cloexec())
		failed++;
	else
		printf("PASS\n");

	if (failed)
		printf("fcntl_fd_test: %d test(s) FAILED\n", failed);
	else
		printf("fcntl_fd_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

/*
 * user/bin/fcntl_dupfd_test.c - fcntl F_DUPFD/F_DUPFD_CLOEXEC tests
 */

#include <ulib.h>

#define CUTEOS_NR_OPEN 32

static int expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int expect_mask(const char *name, long flags, long mask, long want)
{
	return expect_ret(name, flags & mask, want);
}

static int test_dupfd_minimum_and_cloexec(void)
{
	long fd;
	long hold;
	long dupfd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/fcntl_dupfd_min",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open dupfd min: %ld\n", fd);
		return 1;
	}

	hold = openat(AT_FDCWD, "/fcntl_dupfd_hold",
		      O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (hold < 0) {
		printf("FAIL: open dupfd hold: %ld\n", hold);
		close((int)fd);
		unlinkat(AT_FDCWD, "/fcntl_dupfd_min", 0);
		return 1;
	}

	failed += expect_ret("set source cloexec",
			     fcntl((int)fd, F_SETFD, FD_CLOEXEC), 0);
	dupfd = fcntl((int)fd, F_DUPFD, hold);
	if (dupfd < 0) {
		printf("FAIL: F_DUPFD min returned %ld\n", dupfd);
		failed++;
	} else {
		failed += expect_ret("F_DUPFD lowest free", dupfd, hold + 1);
		failed += expect_ret("F_DUPFD clears cloexec",
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
	char *argv[] = {"fcntl_fd_checker", fd_buf, 0};
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

	failed += expect_ret("F_DUPFD_CLOEXEC minimum", dupfd, 10);
	failed += expect_ret("F_DUPFD_CLOEXEC flag",
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
		execve("/bin/fcntl_fd_checker", argv, envp);
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

	fd = openat(AT_FDCWD, "/fcntl_dupfd_flags",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
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

	failed += expect_ret("dupfd F_SETFL O_APPEND",
			     fcntl((int)dupfd, F_SETFL, O_APPEND), 0);
	failed += expect_mask("source sees dupfd append",
			      fcntl((int)fd, F_GETFL, 0), O_APPEND, O_APPEND);
	failed += expect_ret("source clears append", fcntl((int)fd, F_SETFL, 0),
			     0);
	failed += expect_mask("dupfd sees append clear",
			      fcntl((int)dupfd, F_GETFL, 0), O_APPEND, 0);

	close((int)dupfd);
	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_dupfd_flags", 0);
	return failed;
}

static int test_error_paths(void)
{
	long fd;
	long fds[CUTEOS_NR_OPEN];
	int n = 0;
	int failed = 0;

	fd = openat(AT_FDCWD, "/fcntl_dupfd_error",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open dupfd error: %ld\n", fd);
		return 1;
	}

	failed += expect_ret("F_DUPFD invalid oldfd",
			     fcntl(-1, F_DUPFD, (unsigned long)-1), -EBADF);
	failed += expect_ret("F_DUPFD negative min",
			     fcntl((int)fd, F_DUPFD, (unsigned long)-1),
			     -EINVAL);
	failed += expect_ret("F_DUPFD min too high",
			     fcntl((int)fd, F_DUPFD, CUTEOS_NR_OPEN),
			     -EINVAL);
	failed += expect_ret("F_DUPFD_CLOEXEC negative min",
			     fcntl((int)fd, F_DUPFD_CLOEXEC,
				   (unsigned long)-1),
			     -EINVAL);

	while (n < CUTEOS_NR_OPEN) {
		long next = openat(AT_FDCWD, "/fcntl_dupfd_fill",
				   O_CREAT | O_RDWR, 0644);
		if (next < 0)
			break;
		fds[n++] = next;
	}

	failed += expect_ret("F_DUPFD table full",
			     fcntl((int)fd, F_DUPFD, 0), -EMFILE);

	while (n > 0)
		close((int)fds[--n]);
	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_dupfd_error", 0);
	unlinkat(AT_FDCWD, "/fcntl_dupfd_fill", 0);
	return failed;
}

int main(void)
{
	int failed = 0;

	printf("fcntl_dupfd_test: minimum and cloexec reset ... ");
	if (test_dupfd_minimum_and_cloexec())
		failed++;
	else
		printf("PASS\n");

	printf("fcntl_dupfd_test: cloexec duplicate closes on exec ... ");
	if (test_dupfd_cloexec_exec())
		failed++;
	else
		printf("PASS\n");

	printf("fcntl_dupfd_test: status flags shared ... ");
	if (test_dupfd_shares_status_flags())
		failed++;
	else
		printf("PASS\n");

	printf("fcntl_dupfd_test: error paths ... ");
	if (test_error_paths())
		failed++;
	else
		printf("PASS\n");

	if (failed)
		printf("fcntl_dupfd_test: %d test(s) FAILED\n", failed);
	else
		printf("fcntl_dupfd_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

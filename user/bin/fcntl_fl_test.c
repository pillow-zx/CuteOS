/*
 * user/bin/fcntl_fl_test.c - fcntl F_GETFL/F_SETFL status flag tests
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

static int expect_mask(const char *name, long flags, long mask, long want)
{
	return expect_ret(name, flags & mask, want);
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
		failed += expect_mask("F_GETFL access mode", flags, O_RDWR,
				      O_RDWR);
		failed += expect_mask("F_GETFL append", flags, O_APPEND,
				      O_APPEND);
		failed +=
			expect_mask("F_GETFL omits create", flags, O_CREAT, 0);
		failed += expect_mask("F_GETFL omits trunc", flags, O_TRUNC, 0);
		failed += expect_mask("F_GETFL omits cloexec", flags, O_CLOEXEC,
				      0);
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
		failed += expect_mask("F_GETFL dir access mode", flags, O_RDWR,
				      O_RDONLY);
		failed += expect_mask("F_GETFL directory", flags, O_DIRECTORY,
				      O_DIRECTORY);
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
	failed += expect_ret("F_SETFL O_APPEND",
			     fcntl((int)fd, F_SETFL, O_APPEND), 0);
	failed += expect_mask("F_GETFL after set append",
			      fcntl((int)fd, F_GETFL, 0), O_APPEND, O_APPEND);
	write((int)fd, "Z", 1);
	lseek((int)fd, 0, SEEK_SET);
	n = read((int)fd, buf, sizeof(buf));
	if (n != 4 || buf[0] != 'a' || buf[1] != 'b' || buf[2] != 'c' ||
	    buf[3] != 'Z') {
		printf("FAIL: append write content n=%ld\n", n);
		failed++;
	}

	failed += expect_ret("F_SETFL clear append", fcntl((int)fd, F_SETFL, 0),
			     0);
	failed += expect_mask("F_GETFL after clear append",
			      fcntl((int)fd, F_GETFL, 0), O_APPEND, 0);
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

	failed += expect_ret("dup F_SETFL O_APPEND",
			     fcntl((int)dupfd, F_SETFL, O_APPEND), 0);
	failed += expect_mask("original sees dup append",
			      fcntl((int)fd, F_GETFL, 0), O_APPEND, O_APPEND);
	failed += expect_ret("original clears append",
			     fcntl((int)fd, F_SETFL, 0), 0);
	failed += expect_mask("dup sees append clear",
			      fcntl((int)dupfd, F_GETFL, 0), O_APPEND, 0);

	close((int)dupfd);
	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_fl_dup", 0);
	return failed;
}

static int test_error_paths(void)
{
	long fd;
	int failed = 0;

	failed +=
		expect_ret("F_GETFL invalid fd", fcntl(-1, F_GETFL, 0), -EBADF);
	failed +=
		expect_ret("F_SETFL invalid fd", fcntl(-1, F_SETFL, 0), -EBADF);

	fd = openat(AT_FDCWD, "/fcntl_fl_error", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open error paths: %ld\n", fd);
		return failed + 1;
	}

	failed += expect_ret("F_SETFL ignores immutable bits",
			     fcntl((int)fd, F_SETFL,
				   O_APPEND | O_CREAT | O_TRUNC | O_CLOEXEC),
			     0);
	failed += expect_mask("immutable bits not reported",
			      fcntl((int)fd, F_GETFL, 0),
			      O_CREAT | O_TRUNC | O_CLOEXEC, 0);
	failed += expect_ret("F_SETFL rejects O_NONBLOCK",
			     fcntl((int)fd, F_SETFL, O_NONBLOCK), -EINVAL);

	close((int)fd);
	unlinkat(AT_FDCWD, "/fcntl_fl_error", 0);
	return failed;
}

int main(void)
{
	int failed = 0;

	printf("fcntl_fl_test: getfl filters open flags ... ");
	if (test_getfl_filters_open_flags())
		failed++;
	else
		printf("PASS\n");

	printf("fcntl_fl_test: setfl append behavior ... ");
	if (test_setfl_append_behavior())
		failed++;
	else
		printf("PASS\n");

	printf("fcntl_fl_test: setfl shared by dup ... ");
	if (test_setfl_shared_by_dup())
		failed++;
	else
		printf("PASS\n");

	printf("fcntl_fl_test: error paths ... ");
	if (test_error_paths())
		failed++;
	else
		printf("PASS\n");

	if (failed)
		printf("fcntl_fl_test: %d test(s) FAILED\n", failed);
	else
		printf("fcntl_fl_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

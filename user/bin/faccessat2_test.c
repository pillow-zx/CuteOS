/*
 * user/bin/faccessat2_test.c - faccessat2 Linux ABI compatibility tests
 */

#include <ulib.h>

static long faccessat2(int dfd, const char *path, int mode, int flags)
{
	return syscall(SYS_faccessat2, dfd, (long)path, mode, flags);
}

static int expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int test_flags_zero_existing_file(void)
{
	long fd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/faccessat2_basic",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open basic file: %ld\n", fd);
		return 1;
	}

	failed += expect_ret("F_OK", faccessat2(AT_FDCWD,
						"/faccessat2_basic", F_OK, 0),
			     0);
	failed += expect_ret("R_OK", faccessat2(AT_FDCWD,
						"/faccessat2_basic", R_OK, 0),
			     0);
	failed += expect_ret("W_OK", faccessat2(AT_FDCWD,
						"/faccessat2_basic", W_OK, 0),
			     0);

	close((int)fd);
	unlinkat(AT_FDCWD, "/faccessat2_basic", 0);
	return failed;
}

static int test_flag_validation_and_eaccess(void)
{
	long fd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/faccessat2_flags",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open flags file: %ld\n", fd);
		return 1;
	}

	failed += expect_ret("AT_EACCESS", faccessat2(AT_FDCWD,
						      "/faccessat2_flags",
						      F_OK, AT_EACCESS),
			     0);
	failed += expect_ret("bad mode", faccessat2(AT_FDCWD,
						    "/faccessat2_flags",
						    R_OK | 0x80, 0),
			     -EINVAL);
	failed += expect_ret("bad flags", faccessat2(AT_FDCWD,
						     "/faccessat2_flags",
						     F_OK, 0x8000),
			     -EINVAL);

	close((int)fd);
	unlinkat(AT_FDCWD, "/faccessat2_flags", 0);
	return failed;
}

static int test_empty_path(void)
{
	long fd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/faccessat2_empty",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open empty-path file: %ld\n", fd);
		return 1;
	}

	failed += expect_ret("fd empty path",
			     faccessat2((int)fd, "", F_OK, AT_EMPTY_PATH), 0);
	failed += expect_ret("cwd empty path",
			     faccessat2(AT_FDCWD, "", F_OK, AT_EMPTY_PATH), 0);
	failed += expect_ret("bad fd empty path",
			     faccessat2(-1, "", F_OK, AT_EMPTY_PATH), -EBADF);
	failed += expect_ret("empty path without flag",
			     faccessat2((int)fd, "", F_OK, 0), -ENOENT);
	failed += expect_ret("null path",
			     faccessat2((int)fd, NULL, F_OK, AT_EMPTY_PATH),
			     -EFAULT);

	close((int)fd);
	unlinkat(AT_FDCWD, "/faccessat2_empty", 0);
	return failed;
}

static int test_symlink_nofollow_on_regular_paths(void)
{
	long fd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/faccessat2_nofollow",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open nofollow file: %ld\n", fd);
		return 1;
	}

	failed += expect_ret("file nofollow",
			     faccessat2(AT_FDCWD, "/faccessat2_nofollow",
					F_OK, AT_SYMLINK_NOFOLLOW),
			     0);
	failed += expect_ret("dir nofollow",
			     faccessat2(AT_FDCWD, "/", F_OK,
					AT_SYMLINK_NOFOLLOW),
			     0);

	close((int)fd);
	unlinkat(AT_FDCWD, "/faccessat2_nofollow", 0);
	return failed;
}

int main(void)
{
	int failed = 0;

	printf("faccessat2_test: flags zero existing file ... ");
	if (test_flags_zero_existing_file())
		failed++;
	else
		printf("PASS\n");

	printf("faccessat2_test: flag validation and eaccess ... ");
	if (test_flag_validation_and_eaccess())
		failed++;
	else
		printf("PASS\n");

	printf("faccessat2_test: empty path ... ");
	if (test_empty_path())
		failed++;
	else
		printf("PASS\n");

	printf("faccessat2_test: symlink nofollow on regular paths ... ");
	if (test_symlink_nofollow_on_regular_paths())
		failed++;
	else
		printf("PASS\n");

	if (failed)
		printf("faccessat2_test: %d test group(s) FAILED\n", failed);
	else
		printf("faccessat2_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

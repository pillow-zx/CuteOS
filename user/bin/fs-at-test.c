#include <ulib.h>

static int failures;

static void expect_eq_long(const char *name, long got, long want)
{
	if (got != want) {
		printf("fs-at-test: %s got %ld want %ld\n", name, got, want);
		failures++;
	}
}

static void expect_true(const char *name, int ok)
{
	if (!ok) {
		printf("fs-at-test: %s failed\n", name);
		failures++;
	}
}

static void cleanup_path(const char *path, int flags)
{
	long ret = unlinkat(AT_FDCWD, path, flags);

	(void)ret;
}

static void test_openat_uses_dirfd_for_relative_path(void)
{
	struct stat st;
	long dirfd;
	long fd;

	cleanup_path("/fsat-open/base/created", 0);
	cleanup_path("/fsat-open/cwd/created", 0);
	cleanup_path("/fsat-open/base", AT_REMOVEDIR);
	cleanup_path("/fsat-open/cwd", AT_REMOVEDIR);
	cleanup_path("/fsat-open", AT_REMOVEDIR);

	expect_eq_long("mkdir /fsat-open", mkdirat(AT_FDCWD, "/fsat-open", 0777),
		       0);
	expect_eq_long("mkdir /fsat-open/base",
		       mkdirat(AT_FDCWD, "/fsat-open/base", 0777), 0);
	expect_eq_long("mkdir /fsat-open/cwd",
		       mkdirat(AT_FDCWD, "/fsat-open/cwd", 0777), 0);

	dirfd = openat(AT_FDCWD, "/fsat-open/base", O_RDONLY | O_DIRECTORY, 0);
	expect_true("open base dir", dirfd >= 0);
	if (dirfd < 0)
		return;

	expect_eq_long("chdir cwd", chdir("/fsat-open/cwd"), 0);
	fd = openat((int)dirfd, "created", O_CREAT | O_WRONLY, 0644);
	expect_true("openat dirfd create", fd >= 0);
	if (fd >= 0)
		close((int)fd);

	expect_eq_long("created under dirfd",
		       fstatat(AT_FDCWD, "/fsat-open/base/created", &st, 0),
		       0);
	expect_eq_long("not created under cwd",
		       fstatat(AT_FDCWD, "/fsat-open/cwd/created", &st, 0),
		       -ENOENT);

	close((int)dirfd);
}

static void test_mkdirat_and_fstatat_use_dirfd_for_relative_path(void)
{
	struct stat st;
	long dirfd;

	cleanup_path("/fsat-mkdir/base/child", AT_REMOVEDIR);
	cleanup_path("/fsat-mkdir/other/child", AT_REMOVEDIR);
	cleanup_path("/fsat-mkdir/base", AT_REMOVEDIR);
	cleanup_path("/fsat-mkdir/other", AT_REMOVEDIR);
	cleanup_path("/fsat-mkdir", AT_REMOVEDIR);

	expect_eq_long("mkdir /fsat-mkdir",
		       mkdirat(AT_FDCWD, "/fsat-mkdir", 0777), 0);
	expect_eq_long("mkdir /fsat-mkdir/base",
		       mkdirat(AT_FDCWD, "/fsat-mkdir/base", 0777), 0);
	expect_eq_long("mkdir /fsat-mkdir/other",
		       mkdirat(AT_FDCWD, "/fsat-mkdir/other", 0777), 0);

	dirfd = openat(AT_FDCWD, "/fsat-mkdir/base", O_RDONLY | O_DIRECTORY,
		       0);
	expect_true("open mkdir base dir", dirfd >= 0);
	if (dirfd < 0)
		return;

	expect_eq_long("chdir mkdir other", chdir("/fsat-mkdir/other"), 0);
	expect_eq_long("mkdirat dirfd child", mkdirat((int)dirfd, "child", 0755),
		       0);
	expect_eq_long("fstatat dirfd child", fstatat((int)dirfd, "child", &st, 0),
		       0);
	expect_true("fstatat child is dir", S_ISDIR(st.st_mode));
	expect_eq_long("mkdir child not under cwd",
		       fstatat(AT_FDCWD, "child", &st, 0), -ENOENT);

	close((int)dirfd);
}

static void test_unlinkat_uses_dirfd_and_reports_dirfd_errors(void)
{
	struct stat st;
	long dirfd;
	long filefd;
	long fd;

	cleanup_path("/fsat-unlink/base/keep", 0);
	cleanup_path("/fsat-unlink/base/remove", 0);
	cleanup_path("/fsat-unlink/base/filefd", 0);
	cleanup_path("/fsat-unlink/other/remove", 0);
	cleanup_path("/fsat-unlink/base", AT_REMOVEDIR);
	cleanup_path("/fsat-unlink/other", AT_REMOVEDIR);
	cleanup_path("/fsat-unlink", AT_REMOVEDIR);

	expect_eq_long("mkdir /fsat-unlink",
		       mkdirat(AT_FDCWD, "/fsat-unlink", 0777), 0);
	expect_eq_long("mkdir /fsat-unlink/base",
		       mkdirat(AT_FDCWD, "/fsat-unlink/base", 0777), 0);
	expect_eq_long("mkdir /fsat-unlink/other",
		       mkdirat(AT_FDCWD, "/fsat-unlink/other", 0777), 0);

	dirfd = openat(AT_FDCWD, "/fsat-unlink/base", O_RDONLY | O_DIRECTORY,
		       0);
	expect_true("open unlink base dir", dirfd >= 0);
	if (dirfd < 0)
		return;

	fd = openat((int)dirfd, "remove", O_CREAT | O_WRONLY, 0644);
	expect_true("create unlink target", fd >= 0);
	if (fd >= 0)
		close((int)fd);
	fd = openat((int)dirfd, "filefd", O_CREAT | O_WRONLY, 0644);
	expect_true("create non-dir fd target", fd >= 0);
	if (fd >= 0)
		close((int)fd);

	expect_eq_long("chdir unlink other", chdir("/fsat-unlink/other"), 0);
	expect_eq_long("unlinkat bad dirfd", unlinkat(1234, "remove", 0),
		       -EBADF);
	filefd = openat((int)dirfd, "filefd", O_RDONLY, 0);
	expect_true("open non-dir fd", filefd >= 0);
	if (filefd >= 0) {
		expect_eq_long("unlinkat non-dir fd",
			       unlinkat((int)filefd, "remove", 0), -ENOTDIR);
		close((int)filefd);
	}

	expect_eq_long("unlinkat dirfd remove", unlinkat((int)dirfd, "remove", 0),
		       0);
	expect_eq_long("removed under dirfd",
		       fstatat((int)dirfd, "remove", &st, 0), -ENOENT);
	expect_eq_long("not removed under cwd",
		       fstatat(AT_FDCWD, "remove", &st, 0), -ENOENT);

	close((int)dirfd);
}

static void test_newfstatat_empty_path_stats_fd_and_cwd(void)
{
	struct stat st_fd;
	struct stat st_at;
	struct stat st_cwd;
	long fd;

	cleanup_path("/fsat-empty/file", 0);
	cleanup_path("/fsat-empty", AT_REMOVEDIR);

	expect_eq_long("mkdir /fsat-empty",
		       mkdirat(AT_FDCWD, "/fsat-empty", 0777), 0);
	fd = openat(AT_FDCWD, "/fsat-empty/file", O_CREAT | O_RDWR, 0644);
	expect_true("open empty path file", fd >= 0);
	if (fd < 0)
		return;

	expect_eq_long("fstat file", fstat((int)fd, &st_fd), 0);
	expect_eq_long("fstatat empty fd",
		       fstatat((int)fd, "", &st_at, AT_EMPTY_PATH), 0);
	expect_eq_long("empty fd ino", st_at.st_ino, st_fd.st_ino);
	expect_eq_long("empty fd mode", st_at.st_mode, st_fd.st_mode);

	expect_eq_long("chdir /fsat-empty", chdir("/fsat-empty"), 0);
	expect_eq_long("fstatat empty cwd",
		       fstatat(AT_FDCWD, "", &st_cwd, AT_EMPTY_PATH), 0);
	expect_true("empty cwd is dir", S_ISDIR(st_cwd.st_mode));
	expect_eq_long("fstatat null empty fd",
		       fstatat((int)fd, NULL, &st_at, AT_EMPTY_PATH), 0);

	close((int)fd);
}

static void test_mknodat_creates_character_device_under_dirfd(void)
{
	struct stat st;
	unsigned long null_dev = MKDEV(1, 3);
	unsigned long block_dev = MKDEV(8, 0);
	long dirfd;
	long fd;

	cleanup_path("/fsat-mknod/base/null2", 0);
	cleanup_path("/fsat-mknod/base/blk0", 0);
	cleanup_path("/fsat-mknod/other/null2", 0);
	cleanup_path("/fsat-mknod/base", AT_REMOVEDIR);
	cleanup_path("/fsat-mknod/other", AT_REMOVEDIR);
	cleanup_path("/fsat-mknod", AT_REMOVEDIR);

	expect_eq_long("mkdir /fsat-mknod",
		       mkdirat(AT_FDCWD, "/fsat-mknod", 0777), 0);
	expect_eq_long("mkdir /fsat-mknod/base",
		       mkdirat(AT_FDCWD, "/fsat-mknod/base", 0777), 0);
	expect_eq_long("mkdir /fsat-mknod/other",
		       mkdirat(AT_FDCWD, "/fsat-mknod/other", 0777), 0);

	dirfd = openat(AT_FDCWD, "/fsat-mknod/base", O_RDONLY | O_DIRECTORY,
		       0);
	expect_true("open mknod base dir", dirfd >= 0);
	if (dirfd < 0)
		return;

	expect_eq_long("chdir mknod other", chdir("/fsat-mknod/other"), 0);
	expect_eq_long("mknodat char device",
		       mknodat((int)dirfd, "null2", S_IFCHR | 0666, null_dev),
		       0);
	expect_eq_long("fstatat mknod device",
		       fstatat((int)dirfd, "null2", &st, 0), 0);
	expect_true("mknodat device is char", S_ISCHR(st.st_mode));
	expect_eq_long("mknodat device rdev", st.st_rdev, null_dev);
	expect_eq_long("mknodat not under cwd",
		       fstatat(AT_FDCWD, "null2", &st, 0), -ENOENT);
	expect_eq_long("mknodat existing device",
		       mknodat((int)dirfd, "null2", S_IFCHR | 0666, null_dev),
		       -EEXIST);
	expect_eq_long("fstatat existing still valid",
		       fstatat((int)dirfd, "null2", &st, 0), 0);
	expect_true("existing device still char", S_ISCHR(st.st_mode));

	expect_eq_long("mknodat block device",
		       mknodat((int)dirfd, "blk0", S_IFBLK | 0600, block_dev),
		       0);
	expect_eq_long("fstatat block device",
		       fstatat((int)dirfd, "blk0", &st, 0), 0);
	expect_true("mknodat device is block", S_ISBLK(st.st_mode));
	expect_eq_long("mknodat block rdev", st.st_rdev, block_dev);

	fd = openat((int)dirfd, "null2", O_WRONLY, 0);
	expect_true("open mknodat null device", fd >= 0);
	if (fd >= 0)
		close((int)fd);
	close((int)dirfd);
}

static void test_readlinkat_uses_dirfd_and_truncates_without_nul(void)
{
	char buf[9];
	long dirfd;

	memset(buf, 'X', sizeof(buf));
	dirfd = openat(AT_FDCWD, "/fixtures", O_RDONLY | O_DIRECTORY, 0);
	expect_true("open fixtures dir", dirfd >= 0);
	if (dirfd < 0)
		return;

	expect_eq_long("readlinkat dirfd truncated",
		       readlinkat((int)dirfd, "readlink-link", buf, 8), 8);
	expect_true("readlinkat bytes", strncmp(buf, "readlink", 8) == 0);
	expect_eq_long("readlinkat no nul", buf[8], 'X');

	close((int)dirfd);
}

static void test_faccessat_uses_dirfd_for_relative_path(void)
{
	long dirfd;
	long fd;

	cleanup_path("/fsat-access/base/file", 0);
	cleanup_path("/fsat-access/other/file", 0);
	cleanup_path("/fsat-access/base", AT_REMOVEDIR);
	cleanup_path("/fsat-access/other", AT_REMOVEDIR);
	cleanup_path("/fsat-access", AT_REMOVEDIR);

	expect_eq_long("mkdir /fsat-access",
		       mkdirat(AT_FDCWD, "/fsat-access", 0777), 0);
	expect_eq_long("mkdir /fsat-access/base",
		       mkdirat(AT_FDCWD, "/fsat-access/base", 0777), 0);
	expect_eq_long("mkdir /fsat-access/other",
		       mkdirat(AT_FDCWD, "/fsat-access/other", 0777), 0);

	dirfd = openat(AT_FDCWD, "/fsat-access/base", O_RDONLY | O_DIRECTORY,
		       0);
	expect_true("open access base dir", dirfd >= 0);
	if (dirfd < 0)
		return;

	fd = openat((int)dirfd, "file", O_CREAT | O_WRONLY, 0644);
	expect_true("create access file", fd >= 0);
	if (fd >= 0)
		close((int)fd);

	expect_eq_long("chdir access other", chdir("/fsat-access/other"), 0);
	expect_eq_long("faccessat dirfd file",
		       faccessat((int)dirfd, "file", F_OK, 0), 0);
	expect_eq_long("faccessat bad mode",
		       faccessat((int)dirfd, "file", 8, 0), -EINVAL);
	expect_eq_long("faccessat bad dirfd", faccessat(1234, "file", F_OK, 0),
		       -EBADF);
	expect_eq_long("faccessat not under cwd",
		       faccessat(AT_FDCWD, "file", F_OK, 0), -ENOENT);

	close((int)dirfd);
}

int main(void)
{
	test_openat_uses_dirfd_for_relative_path();
	test_mkdirat_and_fstatat_use_dirfd_for_relative_path();
	test_unlinkat_uses_dirfd_and_reports_dirfd_errors();
	test_newfstatat_empty_path_stats_fd_and_cwd();
	test_mknodat_creates_character_device_under_dirfd();
	test_readlinkat_uses_dirfd_and_truncates_without_nul();
	test_faccessat_uses_dirfd_for_relative_path();

	if (failures) {
		printf("fs-at-test: %d failures\n", failures);
		return 1;
	}

	printf("fs-at-test: PASS\n");
	return 0;
}

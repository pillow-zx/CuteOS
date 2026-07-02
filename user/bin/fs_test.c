/*
 * user/bin/fs_test.c - filesystem and pipe user ABI tests
 */

#include <ulib.h>

#define EPIPE 32
#define EXDEV 18
#define EXT2_SUPER_MAGIC 0xef53

#define MOUNT_DEV "/tmp/mount_dev"
#define MOUNT_DIR "/tmp/mount_dir"
#define MOUNT_HIDDEN "/tmp/mount_dir/hidden"
#define MOUNT_CREATED "/tmp/mount_dir/created_on_mount"
#define MOUNT_RENAMED "/tmp/mount_dir/renamed_on_mount"
#define MOUNT_CWD_CREATED "/tmp/mount_dir/cwd_created_on_mount"
#define MOUNT_FD_CREATED "/tmp/mount_dir/fd_created_on_mount"
#define MOUNT_FD_RENAMED "/tmp/mount_dir/fd_renamed_on_mount"

/* Helper: create a file and write data to it. */
static int make_file(const char *path, const char *data, int len)
{
	int fd = openat(AT_FDCWD, path, O_CREAT | O_WRONLY | O_TRUNC, 0644);

	if (fd < 0)
		return fd;
	write(fd, data, len);
	close(fd);
	return 0;
}

/* Helper: read first nbytes from file, return 0 on match. */
static int read_check(const char *path, const char *expect, int len)
{
	char buf[64];
	int fd = openat(AT_FDCWD, path, O_RDONLY, 0);
	int i;

	if (fd < 0) {
		printf("  read_check: open failed fd=%d\n", fd);
		return -1;
	}
	int n = (int)read(fd, buf, len);
	close(fd);
	if (n != len) {
		printf("  read_check: read returned %d (want %d)\n", n, len);
		return -1;
	}
	for (i = 0; i < len; i++) {
		if (buf[i] != expect[i]) {
			printf("  read_check: byte[%d] 0x%02x != 0x%02x\n", i,
			       (unsigned char)buf[i], (unsigned char)expect[i]);
			return -1;
		}
	}
	return 0;
}

/* test 1: basic rename: src disappears, dst appears with right content */
static int test_basic(void)
{
	long ret;
	int fd;

	if (make_file("/tmp/rn_src", "hello", 5) < 0) {
		printf("FAIL: make_file rn_src\n");
		return 1;
	}

	ret = renameat2(AT_FDCWD, "/tmp/rn_src", AT_FDCWD, "/tmp/rn_dst", 0);
	if (ret != 0) {
		printf("FAIL: renameat2 basic: %ld\n", ret);
		unlinkat(AT_FDCWD, "/tmp/rn_src", 0);
		return 1;
	}

	/* src must be gone */
	fd = openat(AT_FDCWD, "/tmp/rn_src", O_RDONLY, 0);
	if (fd >= 0) {
		printf("FAIL: src still exists after rename\n");
		close(fd);
		unlinkat(AT_FDCWD, "/tmp/rn_src", 0);
		unlinkat(AT_FDCWD, "/tmp/rn_dst", 0);
		return 1;
	}

	/* dst must contain "hello" */
	if (read_check("/tmp/rn_dst", "hello", 5) != 0) {
		printf("FAIL: dst data wrong\n");
		unlinkat(AT_FDCWD, "/tmp/rn_dst", 0);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/rn_dst", 0);
	return 0;
}

/* test 2: RENAME_NOREPLACE when dst exists → -EEXIST */
static int test_noreplace(void)
{
	long ret;

	if (make_file("/tmp/nr_src", "x", 1) < 0 ||
	    make_file("/tmp/nr_dst", "y", 1) < 0) {
		printf("FAIL: make files for noreplace test\n");
		unlinkat(AT_FDCWD, "/tmp/nr_src", 0);
		unlinkat(AT_FDCWD, "/tmp/nr_dst", 0);
		return 1;
	}

	ret = renameat2(AT_FDCWD, "/tmp/nr_src", AT_FDCWD, "/tmp/nr_dst",
			RENAME_NOREPLACE);
	if (ret != -EEXIST) {
		printf("FAIL: RENAME_NOREPLACE: expected -EEXIST(%d) got %ld\n",
		       EEXIST, ret);
		unlinkat(AT_FDCWD, "/tmp/nr_src", 0);
		unlinkat(AT_FDCWD, "/tmp/nr_dst", 0);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/nr_src", 0);
	unlinkat(AT_FDCWD, "/tmp/nr_dst", 0);
	return 0;
}

/* test 3: replacing an existing regular file updates dst */
static int test_replace_existing(void)
{
	long ret;

	if (make_file("/tmp/rp_src", "new", 3) < 0 ||
	    make_file("/tmp/rp_dst", "old", 3) < 0) {
		printf("FAIL: make files for replace test\n");
		unlinkat(AT_FDCWD, "/tmp/rp_src", 0);
		unlinkat(AT_FDCWD, "/tmp/rp_dst", 0);
		return 1;
	}

	ret = renameat2(AT_FDCWD, "/tmp/rp_src", AT_FDCWD, "/tmp/rp_dst", 0);
	if (ret != 0) {
		printf("FAIL: replace existing: %ld\n", ret);
		unlinkat(AT_FDCWD, "/tmp/rp_src", 0);
		unlinkat(AT_FDCWD, "/tmp/rp_dst", 0);
		return 1;
	}

	if (read_check("/tmp/rp_dst", "new", 3) != 0) {
		printf("FAIL: replaced dst data wrong\n");
		unlinkat(AT_FDCWD, "/tmp/rp_dst", 0);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/rp_dst", 0);
	return 0;
}

/* test 4: renaming a path to itself is a no-op */
static int test_same_path(void)
{
	long ret;

	if (make_file("/tmp/same_path", "z", 1) < 0) {
		printf("FAIL: make same_path\n");
		return 1;
	}

	ret = renameat2(AT_FDCWD, "/tmp/same_path", AT_FDCWD, "/tmp/same_path",
			0);
	if (ret != 0 || read_check("/tmp/same_path", "z", 1) != 0) {
		printf("FAIL: same path rename ret=%ld\n", ret);
		unlinkat(AT_FDCWD, "/tmp/same_path", 0);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/same_path", 0);
	return 0;
}

/* test 5: reject moving a directory into its own subtree */
static int test_dir_into_subtree(void)
{
	long ret;

	mkdirat(AT_FDCWD, "/tmp/rndir", 0755);
	mkdirat(AT_FDCWD, "/tmp/rndir/sub", 0755);

	ret = renameat2(AT_FDCWD, "/tmp/rndir", AT_FDCWD,
			"/tmp/rndir/sub/moved", 0);
	if (ret != -EINVAL) {
		printf("FAIL: dir into subtree expected -EINVAL got %ld\n",
		       ret);
		unlinkat(AT_FDCWD, "/tmp/rndir/sub", AT_REMOVEDIR);
		unlinkat(AT_FDCWD, "/tmp/rndir", AT_REMOVEDIR);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/rndir/sub", AT_REMOVEDIR);
	unlinkat(AT_FDCWD, "/tmp/rndir", AT_REMOVEDIR);
	return 0;
}

/* test 6: target exists on disk even if this test did not create/cache it */
static int test_noreplace_existing_uncached_target(void)
{
	long ret;

	if (make_file("/tmp/nr_uncached_src", "u", 1) < 0) {
		printf("FAIL: make nr_uncached_src\n");
		return 1;
	}

	ret = renameat2(AT_FDCWD, "/tmp/nr_uncached_src", AT_FDCWD,
			"/bin/signal_test", RENAME_NOREPLACE);
	if (ret != -EEXIST) {
		printf("FAIL: uncached target expected -EEXIST got %ld\n", ret);
		unlinkat(AT_FDCWD, "/tmp/nr_uncached_src", 0);
		return 1;
	}

	if (read_check("/tmp/nr_uncached_src", "u", 1) != 0) {
		printf("FAIL: source changed after failed noreplace\n");
		unlinkat(AT_FDCWD, "/tmp/nr_uncached_src", 0);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/nr_uncached_src", 0);
	return 0;
}

/* test 7: an existing cwd dentry remains usable after its directory is renamed
 */
static int test_rename_cwd_keeps_reference(void)
{
	char cwd[64];
	long ret;

	mkdirat(AT_FDCWD, "/tmp/cwd_old", 0755);
	if (make_file("/tmp/cwd_old/before", "a", 1) < 0) {
		printf("FAIL: make cwd_old/before\n");
		unlinkat(AT_FDCWD, "/tmp/cwd_old", AT_REMOVEDIR);
		return 1;
	}
	if (chdir("/tmp/cwd_old") != 0) {
		printf("FAIL: chdir cwd_old\n");
		unlinkat(AT_FDCWD, "/tmp/cwd_old/before", 0);
		unlinkat(AT_FDCWD, "/tmp/cwd_old", AT_REMOVEDIR);
		return 1;
	}

	ret = renameat2(AT_FDCWD, "/tmp/cwd_old", AT_FDCWD, "/tmp/cwd_new", 0);
	if (ret != 0) {
		printf("FAIL: rename cwd dir ret=%ld\n", ret);
		chdir("/");
		unlinkat(AT_FDCWD, "/tmp/cwd_old/before", 0);
		unlinkat(AT_FDCWD, "/tmp/cwd_old", AT_REMOVEDIR);
		return 1;
	}

	if (make_file("after", "b", 1) < 0 ||
	    read_check("before", "a", 1) != 0 ||
	    read_check("after", "b", 1) != 0) {
		printf("FAIL: relative paths through renamed cwd failed\n");
		chdir("/");
		unlinkat(AT_FDCWD, "/tmp/cwd_new/before", 0);
		unlinkat(AT_FDCWD, "/tmp/cwd_new/after", 0);
		unlinkat(AT_FDCWD, "/tmp/cwd_new", AT_REMOVEDIR);
		return 1;
	}

	ret = getcwd(cwd, sizeof(cwd));
	if (ret < 0 || strcmp(cwd, "/tmp/cwd_new") != 0) {
		printf("FAIL: getcwd after rename ret=%ld cwd=%s\n", ret, cwd);
		chdir("/");
		unlinkat(AT_FDCWD, "/tmp/cwd_new/before", 0);
		unlinkat(AT_FDCWD, "/tmp/cwd_new/after", 0);
		unlinkat(AT_FDCWD, "/tmp/cwd_new", AT_REMOVEDIR);
		return 1;
	}

	chdir("/");
	unlinkat(AT_FDCWD, "/tmp/cwd_new/before", 0);
	unlinkat(AT_FDCWD, "/tmp/cwd_new/after", 0);
	unlinkat(AT_FDCWD, "/tmp/cwd_new", AT_REMOVEDIR);
	return 0;
}

static long faccessat2(int dfd, const char *path, int mode, int flags)
{
	return syscall(SYS_faccessat2, dfd, (long)path, mode, flags);
}

static int fs_access_expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int test_faccessat2_flags_zero_existing_file(void)
{
	long fd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/faccessat2_basic", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open basic file: %ld\n", fd);
		return 1;
	}

	failed += fs_access_expect_ret(
		"F_OK", faccessat2(AT_FDCWD, "/faccessat2_basic", F_OK, 0), 0);
	failed += fs_access_expect_ret(
		"R_OK", faccessat2(AT_FDCWD, "/faccessat2_basic", R_OK, 0), 0);
	failed += fs_access_expect_ret(
		"W_OK", faccessat2(AT_FDCWD, "/faccessat2_basic", W_OK, 0), 0);

	close((int)fd);
	unlinkat(AT_FDCWD, "/faccessat2_basic", 0);
	return failed;
}

static int test_faccessat2_flag_validation_and_eaccess(void)
{
	long fd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/faccessat2_flags", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open flags file: %ld\n", fd);
		return 1;
	}

	failed += fs_access_expect_ret(
		"AT_EACCESS",
		faccessat2(AT_FDCWD, "/faccessat2_flags", F_OK, AT_EACCESS), 0);
	failed += fs_access_expect_ret(
		"bad mode",
		faccessat2(AT_FDCWD, "/faccessat2_flags", R_OK | 0x80, 0),
		-EINVAL);
	failed += fs_access_expect_ret(
		"bad flags",
		faccessat2(AT_FDCWD, "/faccessat2_flags", F_OK, 0x8000),
		-EINVAL);

	close((int)fd);
	unlinkat(AT_FDCWD, "/faccessat2_flags", 0);
	return failed;
}

static int test_faccessat2_empty_path(void)
{
	long fd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/faccessat2_empty", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open empty-path file: %ld\n", fd);
		return 1;
	}

	failed += fs_access_expect_ret(
		"fd empty path", faccessat2((int)fd, "", F_OK, AT_EMPTY_PATH),
		0);
	failed += fs_access_expect_ret(
		"cwd empty path", faccessat2(AT_FDCWD, "", F_OK, AT_EMPTY_PATH),
		0);
	failed += fs_access_expect_ret("bad fd empty path",
				       faccessat2(-1, "", F_OK, AT_EMPTY_PATH),
				       -EBADF);
	failed +=
		fs_access_expect_ret("empty path without flag",
				     faccessat2((int)fd, "", F_OK, 0), -ENOENT);
	failed += fs_access_expect_ret(
		"null path", faccessat2((int)fd, NULL, F_OK, AT_EMPTY_PATH),
		-EFAULT);

	close((int)fd);
	unlinkat(AT_FDCWD, "/faccessat2_empty", 0);
	return failed;
}

static int test_faccessat2_symlink_nofollow_on_regular_paths(void)
{
	long fd;
	int failed = 0;

	fd = openat(AT_FDCWD, "/faccessat2_nofollow",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open nofollow file: %ld\n", fd);
		return 1;
	}

	failed += fs_access_expect_ret("file nofollow",
				       faccessat2(AT_FDCWD,
						  "/faccessat2_nofollow", F_OK,
						  AT_SYMLINK_NOFOLLOW),
				       0);
	failed += fs_access_expect_ret(
		"dir nofollow",
		faccessat2(AT_FDCWD, "/", F_OK, AT_SYMLINK_NOFOLLOW), 0);

	close((int)fd);
	unlinkat(AT_FDCWD, "/faccessat2_nofollow", 0);
	return failed;
}

static int has_nul(const char *s, size_t max)
{
	for (size_t i = 0; i < max; i++) {
		if (s[i] == '\0')
			return 1;
	}

	return 0;
}

static int test_getcwd_return_length(void)
{
	char buf[PATH_MAX];
	long ret;

	if (chdir("/") != 0) {
		printf("FAIL: chdir / failed\n");
		return 1;
	}

	memset(buf, 0xaa, sizeof(buf));
	ret = getcwd(buf, sizeof(buf));
	if (ret != 2) {
		printf("FAIL: getcwd ret expected 2 got %ld\n", ret);
		return 1;
	}
	if (strcmp(buf, "/") != 0) {
		printf("FAIL: getcwd buf expected / got %s\n", buf);
		return 1;
	}
	if (ret != (long)strlen(buf) + 1) {
		printf("FAIL: getcwd ret does not include nul\n");
		return 1;
	}

	return 0;
}

static int test_getdents64_small_buffer(void)
{
	char tiny[1];
	long fd;
	long ret;

	fd = open("/", O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		printf("FAIL: open / for small getdents64: %ld\n", fd);
		return 1;
	}

	ret = getdents64((int)fd, tiny, 0);
	if (ret != -EINVAL) {
		printf("FAIL: getdents64 zero expected -%d got %ld\n", EINVAL,
		       ret);
		close((int)fd);
		return 1;
	}

	ret = getdents64((int)fd, tiny, sizeof(tiny));
	close((int)fd);
	if (ret != -EINVAL) {
		printf("FAIL: getdents64 tiny expected -%d got %ld\n", EINVAL,
		       ret);
		return 1;
	}

	return 0;
}

static int test_getdents64_d_off_resume(void)
{
	char buf[512];
	char next_buf[512];
	struct linux_dirent64 *first;
	struct linux_dirent64 *next;
	size_t name_off = OFFSETOF(struct linux_dirent64, d_name);
	long fd;
	long n;
	long n2;
	long off;

	fd = open("/", O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		printf("FAIL: open / for getdents64: %ld\n", fd);
		return 1;
	}

	n = getdents64((int)fd, buf, sizeof(buf));
	if (n <= 0) {
		printf("FAIL: getdents64 root ret=%ld\n", n);
		close((int)fd);
		return 1;
	}

	first = (struct linux_dirent64 *)buf;
	if (first->d_reclen < name_off + 2 ||
	    first->d_reclen > (unsigned long)n) {
		printf("FAIL: first d_reclen=%u n=%ld\n", first->d_reclen, n);
		close((int)fd);
		return 1;
	}
	if (!has_nul(first->d_name, first->d_reclen - name_off)) {
		printf("FAIL: first d_name not nul terminated\n");
		close((int)fd);
		return 1;
	}
	if (first->d_off <= 0) {
		printf("FAIL: first d_off expected >0 got %ld\n", first->d_off);
		close((int)fd);
		return 1;
	}

	off = lseek((int)fd, first->d_off, SEEK_SET);
	if (off != first->d_off) {
		printf("FAIL: lseek d_off expected %ld got %ld\n", first->d_off,
		       off);
		close((int)fd);
		return 1;
	}

	n2 = getdents64((int)fd, next_buf, sizeof(next_buf));
	close((int)fd);
	if (n2 <= 0) {
		printf("FAIL: getdents64 after d_off ret=%ld\n", n2);
		return 1;
	}

	next = (struct linux_dirent64 *)next_buf;
	if (next->d_reclen < name_off + 2 ||
	    next->d_reclen > (unsigned long)n2) {
		printf("FAIL: next d_reclen=%u n=%ld\n", next->d_reclen, n2);
		return 1;
	}
	if (!has_nul(next->d_name, next->d_reclen - name_off)) {
		printf("FAIL: next d_name not nul terminated\n");
		return 1;
	}
	if (strcmp(first->d_name, next->d_name) == 0) {
		printf("FAIL: d_off resume repeated %s\n", first->d_name);
		return 1;
	}

	return 0;
}

static int test_getdents64_names_are_path_usable(void)
{
	char buf[512];
	char full[PATH_MAX];
	struct stat st;
	long fd;
	long n;
	long off = 0;

	fd = open("/", O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		printf("FAIL: open / for getdents64 path names: %ld\n", fd);
		return 1;
	}

	n = getdents64((int)fd, buf, sizeof(buf));
	close((int)fd);
	if (n <= 0) {
		printf("FAIL: getdents64 path names ret=%ld\n", n);
		return 1;
	}

	while (off < n) {
		struct linux_dirent64 *de =
			(struct linux_dirent64 *)(buf + off);

		if (de->d_reclen == 0 || off + de->d_reclen > n) {
			printf("FAIL: getdents64 bad reclen=%u off=%ld n=%ld\n",
			       de->d_reclen, off, n);
			return 1;
		}
		if (!has_nul(de->d_name, de->d_reclen -
					     OFFSETOF(struct linux_dirent64,
						      d_name))) {
			printf("FAIL: getdents64 path name not terminated\n");
			return 1;
		}
		if (path_join(full, sizeof(full), "/", de->d_name) < 0) {
			printf("FAIL: getdents64 path join for %s\n", de->d_name);
			return 1;
		}
		if (fstatat(AT_FDCWD, full, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			printf("FAIL: getdents64 path unusable: %s\n", full);
			return 1;
		}

		off += de->d_reclen;
	}

	return 0;
}

static int test_pipe_eof(void)
{
	int fds[2];
	char buf[8];
	long ret;
	int i;

	if (pipe(fds) != 0) {
		printf("FAIL: pipe\n");
		return 1;
	}

	ret = write(fds[1], "hello", 5);
	if (ret != 5) {
		printf("FAIL: write expected 5 got %ld\n", ret);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	close(fds[1]);

	ret = read(fds[0], buf, 5);
	if (ret != 5) {
		printf("FAIL: read data ret=%ld\n", ret);
		close(fds[0]);
		return 1;
	}
	for (i = 0; i < 5; i++) {
		if (buf[i] != "hello"[i]) {
			printf("FAIL: read byte %d\n", i);
			close(fds[0]);
			return 1;
		}
	}

	ret = read(fds[0], buf, 1);
	if (ret != 0) {
		printf("FAIL: eof expected 0 got %ld\n", ret);
		close(fds[0]);
		return 1;
	}

	close(fds[0]);
	return 0;
}

static int test_pipe_epipe(void)
{
	struct sigaction ign;
	struct sigaction dfl;
	int fds[2];
	long ret;

	memset(&ign, 0, sizeof(ign));
	ign.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &ign, NULL);

	if (pipe(fds) != 0) {
		printf("FAIL: pipe epipe\n");
		return 1;
	}

	close(fds[0]);
	ret = write(fds[1], "x", 1);
	close(fds[1]);

	memset(&dfl, 0, sizeof(dfl));
	dfl.sa_handler = SIG_DFL;
	sigaction(SIGPIPE, &dfl, NULL);

	if (ret != -EPIPE) {
		printf("FAIL: epipe expected -%d got %ld\n", EPIPE, ret);
		return 1;
	}

	return 0;
}

static int test_utimensat_sets_mtime(void)
{
	struct timespec times[2];
	struct stat st;
	long ret;

	if (make_file("/tmp/utime_file", "t", 1) < 0) {
		printf("FAIL: make utime file\n");
		return 1;
	}

	times[0].tv_sec = 100;
	times[0].tv_nsec = 0;
	times[1].tv_sec = 12345;
	times[1].tv_nsec = 0;
	ret = utimensat(AT_FDCWD, "/tmp/utime_file", times, 0);
	if (ret != 0) {
		printf("FAIL: utimensat expected 0 got %ld\n", ret);
		unlinkat(AT_FDCWD, "/tmp/utime_file", 0);
		return 1;
	}

	ret = fstatat(AT_FDCWD, "/tmp/utime_file", &st, 0);
	if (ret != 0) {
		printf("FAIL: stat utime file got %ld\n", ret);
		unlinkat(AT_FDCWD, "/tmp/utime_file", 0);
		return 1;
	}
	if (st.st_mtime_sec != 12345 || st.st_mtime_nsec != 0) {
		printf("FAIL: mtime expected 12345.0 got %ld.%lu\n",
		       st.st_mtime_sec, st.st_mtime_nsec);
		unlinkat(AT_FDCWD, "/tmp/utime_file", 0);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/utime_file", 0);
	return 0;
}

static int test_empty_path_stat_and_utime(void)
{
	struct timespec times[2];
	struct stat st;
	struct statx stx;
	long fd;
	long ret;

	fd = openat(AT_FDCWD, "/tmp/empty_path_stat", O_CREAT | O_RDWR | O_TRUNC,
		    0644);
	if (fd < 0) {
		printf("FAIL: open empty path stat file: %ld\n", fd);
		return 1;
	}

	ret = fstatat((int)fd, "", &st, AT_EMPTY_PATH);
	if (ret != 0 || st.st_size != 0) {
		printf("FAIL: fstatat empty path ret=%ld size=%ld\n", ret,
		       st.st_size);
		close((int)fd);
		unlinkat(AT_FDCWD, "/tmp/empty_path_stat", 0);
		return 1;
	}

	memset(&stx, 0, sizeof(stx));
	ret = statx((int)fd, "", AT_EMPTY_PATH, STATX_BASIC_STATS, &stx);
	if (ret != 0 || stx.stx_size != 0) {
		printf("FAIL: statx empty path ret=%ld size=%lu\n", ret,
		       stx.stx_size);
		close((int)fd);
		unlinkat(AT_FDCWD, "/tmp/empty_path_stat", 0);
		return 1;
	}

	times[0].tv_sec = 200;
	times[0].tv_nsec = 0;
	times[1].tv_sec = 201;
	times[1].tv_nsec = 0;
	ret = utimensat((int)fd, "", times, AT_EMPTY_PATH);
	if (ret != 0) {
		printf("FAIL: utimensat empty string ret=%ld\n", ret);
		close((int)fd);
		unlinkat(AT_FDCWD, "/tmp/empty_path_stat", 0);
		return 1;
	}

	ret = fstatat((int)fd, "", &st, AT_EMPTY_PATH);
	if (ret != 0 || st.st_mtime_sec != 201) {
		printf("FAIL: fstatat after empty utime ret=%ld mtime=%ld\n",
		       ret, st.st_mtime_sec);
		close((int)fd);
		unlinkat(AT_FDCWD, "/tmp/empty_path_stat", 0);
		return 1;
	}

	times[0].tv_sec = 300;
	times[0].tv_nsec = 0;
	times[1].tv_sec = 301;
	times[1].tv_nsec = 0;
	ret = utimensat((int)fd, NULL, times, AT_EMPTY_PATH);
	if (ret != 0) {
		printf("FAIL: utimensat null empty path ret=%ld\n", ret);
		close((int)fd);
		unlinkat(AT_FDCWD, "/tmp/empty_path_stat", 0);
		return 1;
	}

	ret = fstatat((int)fd, "", &st, AT_EMPTY_PATH);
	close((int)fd);
	unlinkat(AT_FDCWD, "/tmp/empty_path_stat", 0);
	if (ret != 0 || st.st_mtime_sec != 301) {
		printf("FAIL: fstatat after null utime ret=%ld mtime=%ld\n",
		       ret, st.st_mtime_sec);
		return 1;
	}

	return 0;
}

static int test_statx_basic_regular_file(void)
{
	struct statx stx;
	long ret;

	if (make_file("/tmp/statx_file", "statx", 5) < 0) {
		printf("FAIL: make statx file\n");
		return 1;
	}

	memset(&stx, 0, sizeof(stx));
	ret = statx(AT_FDCWD, "/tmp/statx_file", 0, STATX_BASIC_STATS, &stx);
	if (ret != 0) {
		printf("FAIL: statx expected 0 got %ld\n", ret);
		unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
		return 1;
	}

	if ((stx.stx_mask & (STATX_TYPE | STATX_MODE | STATX_NLINK |
			    STATX_INO | STATX_SIZE)) !=
	    (STATX_TYPE | STATX_MODE | STATX_NLINK | STATX_INO |
	     STATX_SIZE)) {
		printf("FAIL: statx mask missing fields 0x%x\n", stx.stx_mask);
		unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
		return 1;
	}
	if ((stx.stx_mode & S_IFMT) != S_IFREG || stx.stx_size != 5 ||
	    stx.stx_nlink != 1 || stx.stx_ino == 0) {
		printf("FAIL: statx fields mode=0%o size=%lu nlink=%u ino=%lu\n",
		       stx.stx_mode, stx.stx_size, stx.stx_nlink,
		       stx.stx_ino);
		unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
	return 0;
}

static int test_symlinkat_creates_readlink_target(void)
{
	char buf[32];
	long ret;

	unlinkat(AT_FDCWD, "/tmp/symlink_link", 0);
	ret = symlinkat("relative-target", AT_FDCWD, "/tmp/symlink_link");
	if (ret != 0) {
		printf("FAIL: symlinkat expected 0 got %ld\n", ret);
		return 1;
	}

	memset(buf, 0, sizeof(buf));
	ret = readlinkat(AT_FDCWD, "/tmp/symlink_link", buf, sizeof(buf));
	if (ret >= 0 && ret < (long)sizeof(buf))
		buf[ret] = '\0';
	if (ret != 15 || strcmp(buf, "relative-target") != 0) {
		printf("FAIL: readlink symlink ret=%ld target=%s\n", ret, buf);
		unlinkat(AT_FDCWD, "/tmp/symlink_link", 0);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/symlink_link", 0);
	return 0;
}

static int test_linkat_regular_file_shares_inode(void)
{
	struct stat src_st;
	struct stat dst_st;
	long ret;

	unlinkat(AT_FDCWD, "/tmp/hard_src", 0);
	unlinkat(AT_FDCWD, "/tmp/hard_dst", 0);
	if (make_file("/tmp/hard_src", "hard", 4) < 0) {
		printf("FAIL: make hard link source\n");
		return 1;
	}

	ret = linkat(AT_FDCWD, "/tmp/hard_src", AT_FDCWD, "/tmp/hard_dst", 0);
	if (ret != 0) {
		printf("FAIL: linkat expected 0 got %ld\n", ret);
		unlinkat(AT_FDCWD, "/tmp/hard_src", 0);
		return 1;
	}

	if (fstatat(AT_FDCWD, "/tmp/hard_src", &src_st, 0) != 0 ||
	    fstatat(AT_FDCWD, "/tmp/hard_dst", &dst_st, 0) != 0) {
		printf("FAIL: stat hard link paths\n");
		unlinkat(AT_FDCWD, "/tmp/hard_src", 0);
		unlinkat(AT_FDCWD, "/tmp/hard_dst", 0);
		return 1;
	}
	if (src_st.st_ino != dst_st.st_ino || src_st.st_nlink != 2 ||
	    dst_st.st_nlink != 2 || read_check("/tmp/hard_dst", "hard", 4)) {
		printf("FAIL: hard link inode/nlink ino=%lu/%lu n=%u/%u\n",
		       src_st.st_ino, dst_st.st_ino, src_st.st_nlink,
		       dst_st.st_nlink);
		unlinkat(AT_FDCWD, "/tmp/hard_src", 0);
		unlinkat(AT_FDCWD, "/tmp/hard_dst", 0);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/hard_src", 0);
	unlinkat(AT_FDCWD, "/tmp/hard_dst", 0);
	return 0;
}

static void mount_test_cleanup(void)
{
	umount2(MOUNT_DIR, 0);
	unlinkat(AT_FDCWD, "/created_on_mount", 0);
	unlinkat(AT_FDCWD, "/renamed_on_mount", 0);
	unlinkat(AT_FDCWD, "/cwd_created_on_mount", 0);
	unlinkat(AT_FDCWD, "/fd_created_on_mount", 0);
	unlinkat(AT_FDCWD, "/fd_renamed_on_mount", 0);
	unlinkat(AT_FDCWD, MOUNT_CREATED, 0);
	unlinkat(AT_FDCWD, MOUNT_RENAMED, 0);
	unlinkat(AT_FDCWD, MOUNT_CWD_CREATED, 0);
	unlinkat(AT_FDCWD, MOUNT_FD_CREATED, 0);
	unlinkat(AT_FDCWD, MOUNT_FD_RENAMED, 0);
	unlinkat(AT_FDCWD, MOUNT_HIDDEN, 0);
	unlinkat(AT_FDCWD, MOUNT_DIR, AT_REMOVEDIR);
	unlinkat(AT_FDCWD, MOUNT_DEV, 0);
}

static int mount_test_setup(void)
{
	mount_test_cleanup();
	if (mknodat(AT_FDCWD, MOUNT_DEV, S_IFBLK | 0600, MKDEV(8, 0)) != 0) {
		printf("FAIL: mknod mount dev\n");
		return 1;
	}
	if (mkdirat(AT_FDCWD, MOUNT_DIR, 0755) != 0) {
		printf("FAIL: mkdir mount dir\n");
		mount_test_cleanup();
		return 1;
	}
	if (make_file(MOUNT_HIDDEN, "h", 1) != 0) {
		printf("FAIL: make mount hidden file\n");
		mount_test_cleanup();
		return 1;
	}

	return 0;
}

static int test_mount_umount_ext2_cycle(void)
{
	struct statfs64 st;
	long fd;
	long ret;

	if (mount_test_setup())
		return 1;

	ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", 0, NULL);
	if (ret != 0) {
		printf("FAIL: mount ext2 expected 0 got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	ret = statfs64(MOUNT_DIR, &st);
	if (ret != 0 || st.f_type != EXT2_SUPER_MAGIC) {
		printf("FAIL: statfs mounted ret=%ld type=%lx\n", ret,
		       st.f_type);
		mount_test_cleanup();
		return 1;
	}

	fd = open(MOUNT_DIR "/bin/sh", O_RDONLY);
	if (fd < 0) {
		printf("FAIL: lookup through mount got %ld\n", fd);
		mount_test_cleanup();
		return 1;
	}
	close((int)fd);

	fd = open(MOUNT_HIDDEN, O_RDONLY);
	if (fd != -ENOENT) {
		printf("FAIL: covered file expected -ENOENT got %ld\n", fd);
		if (fd >= 0)
			close((int)fd);
		mount_test_cleanup();
		return 1;
	}

	ret = umount2(MOUNT_DIR, 0);
	if (ret != 0) {
		printf("FAIL: umount2 expected 0 got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	if (read_check(MOUNT_HIDDEN, "h", 1) != 0) {
		printf("FAIL: covered file not restored after umount\n");
		mount_test_cleanup();
		return 1;
	}

	mount_test_cleanup();
	return 0;
}

static int test_mount_parent_lookup_create_targets_mounted_root(void)
{
	long fd;
	long ret;

	if (mount_test_setup())
		return 1;
	ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", 0, NULL);
	if (ret != 0) {
		printf("FAIL: mount for create parent test got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	if (make_file(MOUNT_CREATED, "m", 1) != 0 ||
	    read_check(MOUNT_CREATED, "m", 1) != 0) {
		printf("FAIL: create under mounted directory not visible\n");
		mount_test_cleanup();
		return 1;
	}

	ret = umount2(MOUNT_DIR, 0);
	if (ret != 0) {
		printf("FAIL: umount after create parent test got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	fd = open(MOUNT_CREATED, O_RDONLY);
	if (fd != -ENOENT) {
		printf("FAIL: created file leaked into covered dir: %ld\n", fd);
		if (fd >= 0)
			close((int)fd);
		mount_test_cleanup();
		return 1;
	}

	mount_test_cleanup();
	return 0;
}

static int test_umount2_busy_open_and_cwd(void)
{
	long fd;
	long ret;

	if (mount_test_setup())
		return 1;
	ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", 0, NULL);
	if (ret != 0) {
		printf("FAIL: mount for busy test got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	fd = open(MOUNT_DIR "/bin/sh", O_RDONLY);
	if (fd < 0) {
		printf("FAIL: open mounted file for busy test got %ld\n", fd);
		mount_test_cleanup();
		return 1;
	}
	ret = umount2(MOUNT_DIR, 0);
	if (ret != -EBUSY) {
		printf("FAIL: open-file busy expected -EBUSY got %ld\n", ret);
		close((int)fd);
		mount_test_cleanup();
		return 1;
	}
	close((int)fd);

	if (chdir(MOUNT_DIR) != 0) {
		printf("FAIL: chdir mounted root\n");
		mount_test_cleanup();
		return 1;
	}
	ret = umount2(MOUNT_DIR, 0);
	if (ret != -EBUSY) {
		printf("FAIL: cwd busy expected -EBUSY got %ld\n", ret);
		chdir("/");
		mount_test_cleanup();
		return 1;
	}
	chdir("/");

	ret = umount2(MOUNT_DIR, 0);
	if (ret != 0) {
		printf("FAIL: umount after releasing busy refs got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	mount_test_cleanup();
	return 0;
}

static int test_mount_cwd_getcwd_path_semantics(void)
{
	char cwd[128];
	long fd;
	long ret;

	if (mount_test_setup())
		return 1;
	ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", 0, NULL);
	if (ret != 0) {
		printf("FAIL: mount for cwd test got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	ret = chdir(MOUNT_DIR);
	if (ret != 0) {
		printf("FAIL: chdir mount dir got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}
	ret = getcwd(cwd, sizeof(cwd));
	if (ret < 0 || strcmp(cwd, MOUNT_DIR) != 0) {
		printf("FAIL: getcwd mount dir ret=%ld cwd=%s\n", ret, cwd);
		chdir("/");
		mount_test_cleanup();
		return 1;
	}

	if (make_file("cwd_created_on_mount", "c", 1) != 0 ||
	    read_check(MOUNT_CWD_CREATED, "c", 1) != 0) {
		printf("FAIL: relative create from mount cwd\n");
		chdir("/");
		mount_test_cleanup();
		return 1;
	}

	chdir("/");
	ret = umount2(MOUNT_DIR, 0);
	if (ret != 0) {
		printf("FAIL: umount after cwd semantics got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	fd = open(MOUNT_CWD_CREATED, O_RDONLY);
	if (fd != -ENOENT) {
		printf("FAIL: cwd-created file leaked into covered dir: %ld\n",
		       fd);
		if (fd >= 0)
			close((int)fd);
		mount_test_cleanup();
		return 1;
	}

	mount_test_cleanup();
	return 0;
}

static int test_mount_dirfd_relative_paths_use_mounted_root(void)
{
	long dirfd = -1;
	long fd;
	long ret;

	if (mount_test_setup())
		return 1;
	ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", 0, NULL);
	if (ret != 0) {
		printf("FAIL: mount for dirfd test got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	dirfd = open(MOUNT_DIR, O_RDONLY | O_DIRECTORY);
	if (dirfd < 0) {
		printf("FAIL: open mounted dirfd got %ld\n", dirfd);
		mount_test_cleanup();
		return 1;
	}

	fd = openat((int)dirfd, "fd_created_on_mount",
		    O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: openat create via mounted dirfd got %ld\n", fd);
		close((int)dirfd);
		mount_test_cleanup();
		return 1;
	}
	write((int)fd, "f", 1);
	close((int)fd);
	if (read_check(MOUNT_FD_CREATED, "f", 1) != 0) {
		printf("FAIL: mounted dirfd create not visible\n");
		close((int)dirfd);
		mount_test_cleanup();
		return 1;
	}

	ret = renameat2((int)dirfd, "fd_created_on_mount", (int)dirfd,
			"fd_renamed_on_mount", 0);
	if (ret != 0 || read_check(MOUNT_FD_RENAMED, "f", 1) != 0) {
		printf("FAIL: mounted dirfd rename ret=%ld\n", ret);
		close((int)dirfd);
		mount_test_cleanup();
		return 1;
	}

	ret = unlinkat((int)dirfd, "fd_renamed_on_mount", 0);
	if (ret != 0) {
		printf("FAIL: mounted dirfd unlink ret=%ld\n", ret);
		close((int)dirfd);
		mount_test_cleanup();
		return 1;
	}

	ret = umount2(MOUNT_DIR, 0);
	if (ret != -EBUSY) {
		printf("FAIL: mounted dirfd busy expected -EBUSY got %ld\n",
		       ret);
		close((int)dirfd);
		mount_test_cleanup();
		return 1;
	}
	close((int)dirfd);

	ret = umount2(MOUNT_DIR, 0);
	if (ret != 0) {
		printf("FAIL: umount after dirfd close got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}
	fd = open(MOUNT_FD_CREATED, O_RDONLY);
	if (fd != -ENOENT) {
		printf("FAIL: dirfd-created file leaked into covered dir: %ld\n",
		       fd);
		if (fd >= 0)
			close((int)fd);
		mount_test_cleanup();
		return 1;
	}

	mount_test_cleanup();
	return 0;
}

static int test_mount_unlink_rename_targets_mounted_root(void)
{
	long fd;
	long ret;

	if (mount_test_setup())
		return 1;
	ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", 0, NULL);
	if (ret != 0) {
		printf("FAIL: mount for unlink/rename test got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	if (make_file(MOUNT_CREATED, "u", 1) != 0) {
		printf("FAIL: create unlink target under mount\n");
		mount_test_cleanup();
		return 1;
	}
	ret = unlinkat(AT_FDCWD, MOUNT_CREATED, 0);
	if (ret != 0) {
		printf("FAIL: unlink under mount got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}
	fd = open(MOUNT_CREATED, O_RDONLY);
	if (fd != -ENOENT) {
		printf("FAIL: unlinked mounted file still visible: %ld\n", fd);
		if (fd >= 0)
			close((int)fd);
		mount_test_cleanup();
		return 1;
	}

	if (make_file(MOUNT_CREATED, "r", 1) != 0) {
		printf("FAIL: create rename target under mount\n");
		mount_test_cleanup();
		return 1;
	}
	ret = renameat2(AT_FDCWD, MOUNT_CREATED, AT_FDCWD, MOUNT_RENAMED, 0);
	if (ret != 0 || read_check(MOUNT_RENAMED, "r", 1) != 0) {
		printf("FAIL: rename under mount ret=%ld\n", ret);
		mount_test_cleanup();
		return 1;
	}
	fd = open(MOUNT_CREATED, O_RDONLY);
	if (fd != -ENOENT) {
		printf("FAIL: renamed source still visible: %ld\n", fd);
		if (fd >= 0)
			close((int)fd);
		mount_test_cleanup();
		return 1;
	}

	ret = umount2(MOUNT_DIR, 0);
	if (ret != 0) {
		printf("FAIL: umount after unlink/rename test got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}
	fd = open(MOUNT_RENAMED, O_RDONLY);
	if (fd != -ENOENT) {
		printf("FAIL: renamed file leaked into covered dir: %ld\n", fd);
		if (fd >= 0)
			close((int)fd);
		mount_test_cleanup();
		return 1;
	}

	mount_test_cleanup();
	return 0;
}

static int test_mount_rename_cross_superblock_rejected(void)
{
	long fd;
	long ret;

	if (mount_test_setup())
		return 1;
	ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", 0, NULL);
	if (ret != 0) {
		printf("FAIL: mount for cross-superblock rename got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	if (make_file(MOUNT_CREATED, "x", 1) != 0) {
		printf("FAIL: create cross-superblock rename source\n");
		mount_test_cleanup();
		return 1;
	}

	ret = renameat2(AT_FDCWD, MOUNT_CREATED, AT_FDCWD,
			"/cross_superblock_dst", 0);
	if (ret != -EXDEV) {
		printf("FAIL: cross-superblock rename expected -EXDEV got %ld\n",
		       ret);
		mount_test_cleanup();
		return 1;
	}
	if (read_check(MOUNT_CREATED, "x", 1) != 0) {
		printf("FAIL: cross-superblock rename source changed\n");
		mount_test_cleanup();
		return 1;
	}
	fd = open("/cross_superblock_dst", O_RDONLY);
	if (fd != -ENOENT) {
		printf("FAIL: cross-superblock rename created dst: %ld\n", fd);
		if (fd >= 0)
			close((int)fd);
		mount_test_cleanup();
		return 1;
	}

	mount_test_cleanup();
	return 0;
}

static int test_mount_dotdot_escapes_to_parent(void)
{
	struct stat parent_st;
	struct stat dotdot_st;
	long ret;

	if (mount_test_setup())
		return 1;
	ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", 0, NULL);
	if (ret != 0) {
		printf("FAIL: mount for dotdot test got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}

	if (fstatat(AT_FDCWD, "/tmp", &parent_st, 0) != 0 ||
	    fstatat(AT_FDCWD, MOUNT_DIR "/..", &dotdot_st, 0) != 0) {
		printf("FAIL: stat dotdot mount paths\n");
		mount_test_cleanup();
		return 1;
	}
	if (parent_st.st_ino != dotdot_st.st_ino ||
	    parent_st.st_dev != dotdot_st.st_dev) {
		printf("FAIL: mount dotdot did not reach parent\n");
		mount_test_cleanup();
		return 1;
	}

	mount_test_cleanup();
	return 0;
}

static int test_mount_umount_error_returns(void)
{
	long ret;
	int failed = 0;

	if (mount_test_setup())
		return 1;

	ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", 1, NULL);
	if (ret != -EINVAL) {
		printf("FAIL: mount flags expected -EINVAL got %ld\n", ret);
		failed++;
	}
	ret = umount2(MOUNT_DIR, 1);
	if (ret != -EINVAL) {
		printf("FAIL: umount flags expected -EINVAL got %ld\n", ret);
		failed++;
	}
	ret = mount(MOUNT_DEV, MOUNT_DIR, "no_such_fs", 0, NULL);
	if (ret != -ENODEV) {
		printf("FAIL: unknown fs expected -ENODEV got %ld\n", ret);
		failed++;
	}
	ret = mount(MOUNT_HIDDEN, MOUNT_DIR, "ext2", 0, NULL);
	if (ret != -ENOTBLK) {
		printf("FAIL: non-block source expected -ENOTBLK got %ld\n",
		       ret);
		failed++;
	}
	ret = mount(MOUNT_DEV, MOUNT_HIDDEN, "ext2", 0, NULL);
	if (ret != -ENOTDIR) {
		printf("FAIL: non-dir target expected -ENOTDIR got %ld\n", ret);
		failed++;
	}

	ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", 0, NULL);
	if (ret != 0) {
		printf("FAIL: mount before duplicate got %ld\n", ret);
		mount_test_cleanup();
		return 1;
	}
	ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", 0, NULL);
	if (ret != -EBUSY) {
		printf("FAIL: duplicate mount expected -EBUSY got %ld\n", ret);
		failed++;
	}

	mount_test_cleanup();
	return failed;
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("fs_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(void)
{
	int failed = 0;

	mkdirat(AT_FDCWD, "/tmp", 0755);

	report_group("rename basic", test_basic(), &failed);
	report_group("rename noreplace", test_noreplace(), &failed);
	report_group("rename replace existing", test_replace_existing(),
		     &failed);
	report_group("rename same path", test_same_path(), &failed);
	report_group("rename dir into subtree", test_dir_into_subtree(),
		     &failed);
	report_group("rename noreplace uncached target",
		     test_noreplace_existing_uncached_target(), &failed);
	report_group("rename cwd keeps reference",
		     test_rename_cwd_keeps_reference(), &failed);
	report_group("faccessat2 flags zero existing file",
		     test_faccessat2_flags_zero_existing_file(), &failed);
	report_group("faccessat2 flag validation and eaccess",
		     test_faccessat2_flag_validation_and_eaccess(), &failed);
	report_group("faccessat2 empty path", test_faccessat2_empty_path(),
		     &failed);
	report_group("faccessat2 symlink nofollow on regular paths",
		     test_faccessat2_symlink_nofollow_on_regular_paths(),
		     &failed);
	report_group("getcwd raw return", test_getcwd_return_length(), &failed);
	report_group("getdents64 tiny buffer", test_getdents64_small_buffer(),
		     &failed);
	report_group("getdents64 d_off resume", test_getdents64_d_off_resume(),
		     &failed);
	report_group("getdents64 names are path usable",
		     test_getdents64_names_are_path_usable(), &failed);
	report_group("pipe eof", test_pipe_eof(), &failed);
	report_group("pipe epipe", test_pipe_epipe(), &failed);
	report_group("utimensat sets mtime", test_utimensat_sets_mtime(),
		     &failed);
	report_group("empty path stat and utime",
		     test_empty_path_stat_and_utime(), &failed);
	report_group("statx basic regular file", test_statx_basic_regular_file(),
		     &failed);
	report_group("symlinkat creates readlink target",
		     test_symlinkat_creates_readlink_target(), &failed);
	report_group("linkat regular file shares inode",
		     test_linkat_regular_file_shares_inode(), &failed);
	report_group("mount ext2 and umount2 cycle",
		     test_mount_umount_ext2_cycle(), &failed);
	report_group("mount parent lookup create targets mounted root",
		     test_mount_parent_lookup_create_targets_mounted_root(),
		     &failed);
	report_group("mount unlink and rename target mounted root",
		     test_mount_unlink_rename_targets_mounted_root(), &failed);
	report_group("mount rejects cross-superblock rename",
		     test_mount_rename_cross_superblock_rejected(), &failed);
	report_group("mount cwd and getcwd path semantics",
		     test_mount_cwd_getcwd_path_semantics(), &failed);
	report_group("mount dirfd relative paths use mounted root",
		     test_mount_dirfd_relative_paths_use_mounted_root(), &failed);
	report_group("umount2 busy open file and cwd",
		     test_umount2_busy_open_and_cwd(), &failed);
	report_group("mount dotdot escapes to parent",
		     test_mount_dotdot_escapes_to_parent(), &failed);
	report_group("mount and umount2 error returns",
		     test_mount_umount_error_returns(), &failed);

	if (failed)
		printf("fs_test: %d test group(s) FAILED\n", failed);
	else
		printf("fs_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

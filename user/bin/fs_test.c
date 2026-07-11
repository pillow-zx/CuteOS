/*
 * user/bin/fs_test.c - filesystem and pipe user ABI tests
 */

#include <ulib.h>

#define EPIPE 32
#define EXDEV 18
#define EFBIG 27
#define EXT2_SUPER_MAGIC 0xef53
#define EXT2_BLOCK_SIZE 4096

#define MOUNT_DEV "/tmp/mount_dev"
#define MOUNT_DIR "/tmp/mount_dir"
#define MOUNT_HIDDEN "/tmp/mount_dir/hidden"
#define MOUNT_CREATED "/tmp/mount_dir/created_on_mount"
#define MOUNT_RENAMED "/tmp/mount_dir/renamed_on_mount"
#define MOUNT_CWD_CREATED "/tmp/mount_dir/cwd_created_on_mount"
#define MOUNT_FD_CREATED "/tmp/mount_dir/fd_created_on_mount"
#define MOUNT_FD_RENAMED "/tmp/mount_dir/fd_renamed_on_mount"

static int make_file(const char *path, const char *data, int len)
{
	int fd = openat(AT_FDCWD, path, O_CREAT | O_WRONLY | O_TRUNC, 0644);

	if (fd < 0)
		return fd;
	write(fd, data, len);
	close(fd);
	return 0;
}

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


	fd = openat(AT_FDCWD, "/tmp/rn_src", O_RDONLY, 0);
	if (fd >= 0) {
		printf("FAIL: src still exists after rename\n");
		close(fd);
		unlinkat(AT_FDCWD, "/tmp/rn_src", 0);
		unlinkat(AT_FDCWD, "/tmp/rn_dst", 0);
		return 1;
	}


	if (read_check("/tmp/rn_dst", "hello", 5) != 0) {
		printf("FAIL: dst data wrong\n");
		unlinkat(AT_FDCWD, "/tmp/rn_dst", 0);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/rn_dst", 0);
	return 0;
}

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

static int test_sendfile_regular_to_regular_offset(void)
{
	long in_fd;
	long out_fd;
	long offset = 4;
	long in_pos;
	long ret;
	int failed = 0;

	if (make_file("/tmp/sendfile_src", "0123456789abcdef", 16) < 0) {
		printf("FAIL: make sendfile source\n");
		return 1;
	}

	in_fd = openat(AT_FDCWD, "/tmp/sendfile_src", O_RDONLY, 0);
	out_fd = openat(AT_FDCWD, "/tmp/sendfile_dst",
			O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (in_fd < 0 || out_fd < 0) {
		printf("FAIL: open sendfile files in=%ld out=%ld\n", in_fd,
		       out_fd);
		if (in_fd >= 0)
			close((int)in_fd);
		if (out_fd >= 0)
			close((int)out_fd);
		unlinkat(AT_FDCWD, "/tmp/sendfile_src", 0);
		unlinkat(AT_FDCWD, "/tmp/sendfile_dst", 0);
		return 1;
	}

	ret = sendfile((int)out_fd, (int)in_fd, &offset, 6);
	if (ret != 6) {
		printf("FAIL: sendfile regular expected 6 got %ld\n", ret);
		failed++;
	}
	if (offset != 10) {
		printf("FAIL: sendfile offset expected 10 got %ld\n", offset);
		failed++;
	}
	in_pos = lseek((int)in_fd, 0, SEEK_CUR);
	if (in_pos != 0) {
		printf("FAIL: sendfile explicit offset moved input to %ld\n",
		       in_pos);
		failed++;
	}

	close((int)in_fd);
	close((int)out_fd);
	if (read_check("/tmp/sendfile_dst", "456789", 6) != 0)
		failed++;

	unlinkat(AT_FDCWD, "/tmp/sendfile_src", 0);
	unlinkat(AT_FDCWD, "/tmp/sendfile_dst", 0);
	return failed;
}

static int test_sendfile_file_to_pipe_null_offset(void)
{
	char buf[16];
	int pipefd[2];
	long in_fd;
	long in_pos;
	long ret;
	int failed = 0;

	if (make_file("/tmp/sendfile_pipe_src", "pipe-data", 9) < 0) {
		printf("FAIL: make sendfile pipe source\n");
		return 1;
	}

	in_fd = openat(AT_FDCWD, "/tmp/sendfile_pipe_src", O_RDONLY, 0);
	if (in_fd < 0 || pipe(pipefd) != 0) {
		printf("FAIL: open pipe sendfile source/fds in=%ld\n", in_fd);
		if (in_fd >= 0)
			close((int)in_fd);
		unlinkat(AT_FDCWD, "/tmp/sendfile_pipe_src", 0);
		return 1;
	}

	ret = sendfile(pipefd[1], (int)in_fd, NULL, 7);
	if (ret != 7) {
		printf("FAIL: sendfile pipe expected 7 got %ld\n", ret);
		failed++;
	}
	in_pos = lseek((int)in_fd, 0, SEEK_CUR);
	if (in_pos != 7) {
		printf("FAIL: sendfile null offset input pos expected 7 got %ld\n",
		       in_pos);
		failed++;
	}

	memset(buf, 0, sizeof(buf));
	ret = read(pipefd[0], buf, 7);
	if (ret != 7 || strncmp(buf, "pipe-da", 7) != 0) {
		printf("FAIL: sendfile pipe read ret=%ld data=%s\n", ret, buf);
		failed++;
	}

	close(pipefd[0]);
	close(pipefd[1]);
	close((int)in_fd);
	unlinkat(AT_FDCWD, "/tmp/sendfile_pipe_src", 0);
	return failed;
}

static int test_sendfile_file_to_stdout(void)
{
	long in_fd;
	long offset = 0;
	long ret;

	if (make_file("/tmp/sendfile_stdout_src", "sfout", 5) < 0) {
		printf("FAIL: make sendfile stdout source\n");
		return 1;
	}

	in_fd = openat(AT_FDCWD, "/tmp/sendfile_stdout_src", O_RDONLY, 0);
	if (in_fd < 0) {
		printf("FAIL: open sendfile stdout source: %ld\n", in_fd);
		unlinkat(AT_FDCWD, "/tmp/sendfile_stdout_src", 0);
		return 1;
	}

	ret = sendfile(1, (int)in_fd, &offset, 5);
	close((int)in_fd);
	unlinkat(AT_FDCWD, "/tmp/sendfile_stdout_src", 0);
	if (ret != 5 || offset != 5) {
		printf("FAIL: sendfile stdout ret=%ld offset=%ld\n", ret,
		       offset);
		return 1;
	}

	return 0;
}

static int test_sendfile_rejects_append_output(void)
{
	long in_fd;
	long out_fd;
	long offset = 0;
	long in_pos;
	long ret;
	int failed = 0;

	if (make_file("/tmp/sendfile_append_src", "abc", 3) < 0 ||
	    make_file("/tmp/sendfile_append_dst", "old", 3) < 0) {
		printf("FAIL: make sendfile append files\n");
		unlinkat(AT_FDCWD, "/tmp/sendfile_append_src", 0);
		unlinkat(AT_FDCWD, "/tmp/sendfile_append_dst", 0);
		return 1;
	}

	in_fd = openat(AT_FDCWD, "/tmp/sendfile_append_src", O_RDONLY, 0);
	out_fd = openat(AT_FDCWD, "/tmp/sendfile_append_dst",
			O_WRONLY | O_APPEND, 0);
	if (in_fd < 0 || out_fd < 0) {
		printf("FAIL: open sendfile append fds in=%ld out=%ld\n", in_fd,
		       out_fd);
		if (in_fd >= 0)
			close((int)in_fd);
		if (out_fd >= 0)
			close((int)out_fd);
		unlinkat(AT_FDCWD, "/tmp/sendfile_append_src", 0);
		unlinkat(AT_FDCWD, "/tmp/sendfile_append_dst", 0);
		return 1;
	}

	ret = sendfile((int)out_fd, (int)in_fd, &offset, 3);
	if (ret != -EINVAL) {
		printf("FAIL: sendfile append expected -EINVAL got %ld\n", ret);
		failed++;
	}
	if (offset != 0) {
		printf("FAIL: sendfile append changed offset to %ld\n", offset);
		failed++;
	}
	in_pos = lseek((int)in_fd, 0, SEEK_CUR);
	if (in_pos != 0) {
		printf("FAIL: sendfile append moved input to %ld\n", in_pos);
		failed++;
	}

	close((int)in_fd);
	close((int)out_fd);
	if (read_check("/tmp/sendfile_append_dst", "old", 3) != 0)
		failed++;

	unlinkat(AT_FDCWD, "/tmp/sendfile_append_src", 0);
	unlinkat(AT_FDCWD, "/tmp/sendfile_append_dst", 0);
	return failed;
}

static int test_splice_pipe_to_file_explicit_offset(void)
{
	int pipefd[2];
	long out_fd;
	long off_out = 4;
	long out_pos;
	long ret;
	int failed = 0;

	out_fd = openat(AT_FDCWD, "/tmp/splice_pipe_dst",
			O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (out_fd < 0 || pipe(pipefd) != 0) {
		printf("FAIL: open splice pipe->file fds out=%ld\n", out_fd);
		if (out_fd >= 0)
			close((int)out_fd);
		unlinkat(AT_FDCWD, "/tmp/splice_pipe_dst", 0);
		return 1;
	}

	ret = write(pipefd[1], "pipe-data", 9);
	if (ret != 9) {
		printf("FAIL: write splice pipe input ret=%ld\n", ret);
		failed++;
	}

	ret = splice(pipefd[0], NULL, (int)out_fd, &off_out, 9, 0);
	if (ret != 9) {
		printf("FAIL: splice pipe->file expected 9 got %ld\n", ret);
		failed++;
	}
	if (off_out != 13) {
		printf("FAIL: splice pipe->file offset expected 13 got %ld\n",
		       off_out);
		failed++;
	}
	out_pos = lseek((int)out_fd, 0, SEEK_CUR);
	if (out_pos != 0) {
		printf("FAIL: splice explicit output offset moved fd to %ld\n",
		       out_pos);
		failed++;
	}

	close(pipefd[0]);
	close(pipefd[1]);
	close((int)out_fd);
	if (read_check("/tmp/splice_pipe_dst", "\0\0\0\0pipe-data", 13) != 0)
		failed++;

	unlinkat(AT_FDCWD, "/tmp/splice_pipe_dst", 0);
	return failed;
}

static int test_splice_pipe_to_file_write_error_preserves_pipe(void)
{
	char buf[8];
	int pipefd[2];
	long out_fd;
	long off_out = 0xffffffffL;
	long ret;
	int failed = 0;

	out_fd = openat(AT_FDCWD, "/tmp/splice_pipe_err_dst",
			O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (out_fd < 0 || pipe(pipefd) != 0) {
		printf("FAIL: open splice pipe error fds out=%ld\n", out_fd);
		if (out_fd >= 0)
			close((int)out_fd);
		unlinkat(AT_FDCWD, "/tmp/splice_pipe_err_dst", 0);
		return 1;
	}

	ret = write(pipefd[1], "keep", 4);
	if (ret != 4) {
		printf("FAIL: write splice preserve pipe ret=%ld\n", ret);
		failed++;
	}

	ret = splice(pipefd[0], NULL, (int)out_fd, &off_out, 4, 0);
	if (ret != -EFBIG) {
		printf("FAIL: splice write error expected -EFBIG got %ld\n", ret);
		failed++;
	}
	if (off_out != 0xffffffffL) {
		printf("FAIL: splice write error changed offset to %ld\n", off_out);
		failed++;
	}

	close(pipefd[1]);
	memset(buf, 0, sizeof(buf));
	ret = read(pipefd[0], buf, 4);
	if (ret != 4 || strncmp(buf, "keep", 4) != 0) {
		printf("FAIL: splice write error pipe ret=%ld data=%s\n", ret,
		       buf);
		failed++;
	}

	close(pipefd[0]);
	close((int)out_fd);
	unlinkat(AT_FDCWD, "/tmp/splice_pipe_err_dst", 0);
	return failed;
}

static int test_splice_file_to_pipe_explicit_offset_and_eof(void)
{
	char buf[16];
	int pipefd[2];
	long in_fd;
	long off_in = 4;
	long eof_off = 10;
	long in_pos;
	long ret;
	int failed = 0;

	if (make_file("/tmp/splice_file_src", "0123456789", 10) < 0) {
		printf("FAIL: make splice file source\n");
		return 1;
	}

	in_fd = openat(AT_FDCWD, "/tmp/splice_file_src", O_RDONLY, 0);
	if (in_fd < 0 || pipe(pipefd) != 0) {
		printf("FAIL: open splice file->pipe fds in=%ld\n", in_fd);
		if (in_fd >= 0)
			close((int)in_fd);
		unlinkat(AT_FDCWD, "/tmp/splice_file_src", 0);
		return 1;
	}

	ret = splice((int)in_fd, &off_in, pipefd[1], NULL, 5, 0);
	if (ret != 5) {
		printf("FAIL: splice file->pipe expected 5 got %ld\n", ret);
		failed++;
	}
	if (off_in != 9) {
		printf("FAIL: splice file->pipe offset expected 9 got %ld\n",
		       off_in);
		failed++;
	}
	in_pos = lseek((int)in_fd, 0, SEEK_CUR);
	if (in_pos != 0) {
		printf("FAIL: splice explicit input offset moved fd to %ld\n",
		       in_pos);
		failed++;
	}

	if (ret == 5) {
		memset(buf, 0, sizeof(buf));
		ret = read(pipefd[0], buf, 5);
		if (ret != 5 || strncmp(buf, "45678", 5) != 0) {
			printf("FAIL: splice file->pipe read ret=%ld data=%s\n",
			       ret, buf);
			failed++;
		}
	}

	ret = splice((int)in_fd, &eof_off, pipefd[1], NULL, 5, 0);
	if (ret != 0 || eof_off != 10) {
		printf("FAIL: splice file->pipe eof ret=%ld off=%ld\n", ret,
		       eof_off);
		failed++;
	}

	close(pipefd[0]);
	close(pipefd[1]);
	close((int)in_fd);
	unlinkat(AT_FDCWD, "/tmp/splice_file_src", 0);
	return failed;
}

static int test_statx_basic_regular_file(void)
{
	struct stat st;
	struct statx stx;
	long ret;

	if (make_file("/tmp/statx_file", "statx", 5) < 0) {
		printf("FAIL: make statx file\n");
		return 1;
	}

	memset(&st, 0, sizeof(st));
	ret = fstatat(AT_FDCWD, "/tmp/statx_file", &st, 0);
	if (ret != 0) {
		printf("FAIL: stat before statx got %ld\n", ret);
		unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
		return 1;
	}

	memset(&stx, 0, sizeof(stx));
	ret = statx(AT_FDCWD, "/tmp/statx_file", 0,
		    STATX_ALL | STATX_MNT_ID | STATX_DIOALIGN |
			    STATX_MNT_ID_UNIQUE | STATX_SUBVOL,
		    &stx);
	if (ret != 0) {
		printf("FAIL: statx expected 0 got %ld\n", ret);
		unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
		return 1;
	}

	if (stx.stx_mask != STATX_BASIC_STATS) {
		printf("FAIL: statx mask expected 0x%x got 0x%x\n",
		       STATX_BASIC_STATS, stx.stx_mask);
		unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
		return 1;
	}
	if (stx.stx_blksize != EXT2_BLOCK_SIZE ||
	    stx.stx_attributes != 0 || stx.stx_attributes_mask != 0 ||
	    stx.stx_mnt_id != 0 || stx.stx_dio_mem_align != 0 ||
	    stx.stx_dio_offset_align != 0 || stx.stx_subvol != 0 ||
	    stx.stx_btime.tv_sec != 0 || stx.stx_btime.tv_nsec != 0) {
		printf("FAIL: statx unsupported fields mask=0x%x attr=%lu\n",
		       stx.stx_mask, stx.stx_attributes);
		unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
		return 1;
	}
	if ((stx.stx_mode & S_IFMT) != S_IFREG ||
	    (stx.stx_mode & 0777) != 0644 || stx.stx_size != 5 ||
	    stx.stx_nlink != 1 || stx.stx_ino != st.st_ino ||
	    stx.stx_uid != st.st_uid || stx.stx_gid != st.st_gid ||
	    stx.stx_blocks != st.st_blocks) {
		printf("FAIL: statx basic mode=0%o size=%lu nlink=%u ino=%lu\n",
		       stx.stx_mode, stx.stx_size, stx.stx_nlink,
		       stx.stx_ino);
		unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
		return 1;
	}
	if (stx.stx_atime.tv_sec != st.st_atime_sec ||
	    stx.stx_mtime.tv_sec != st.st_mtime_sec ||
	    stx.stx_ctime.tv_sec != st.st_ctime_sec ||
	    stx.stx_atime.tv_nsec != st.st_atime_nsec ||
	    stx.stx_mtime.tv_nsec != st.st_mtime_nsec ||
	    stx.stx_ctime.tv_nsec != st.st_ctime_nsec) {
		printf("FAIL: statx timestamps mismatch\n");
		unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
		return 1;
	}
	if (stx.stx_dev_major != MAJOR(st.st_dev) ||
	    stx.stx_dev_minor != MINOR(st.st_dev) ||
	    stx.stx_rdev_major != MAJOR(st.st_rdev) ||
	    stx.stx_rdev_minor != MINOR(st.st_rdev)) {
		printf("FAIL: statx dev mismatch dev=%u:%u rdev=%u:%u\n",
		       stx.stx_dev_major, stx.stx_dev_minor,
		       stx.stx_rdev_major, stx.stx_rdev_minor);
		unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
		return 1;
	}

	unlinkat(AT_FDCWD, "/tmp/statx_file", 0);
	return 0;
}

static int statfs_basic_fields_ok(const char *name, const struct statfs64 *st)
{
	int i;

	if (st->f_type != EXT2_SUPER_MAGIC || st->f_bsize != EXT2_BLOCK_SIZE ||
	    st->f_frsize != EXT2_BLOCK_SIZE || st->f_blocks == 0 ||
	    st->f_bfree > st->f_blocks || st->f_bavail > st->f_bfree ||
	    st->f_files == 0 || st->f_ffree > st->f_files ||
	    st->f_namelen != 255 || st->f_flags != 0) {
		printf("FAIL: %s statfs fields type=%lx bsize=%ld blocks=%lu\n",
		       name, st->f_type, st->f_bsize, st->f_blocks);
		return 0;
	}
	if (st->f_fsid[0] == 0 && st->f_fsid[1] == 0) {
		printf("FAIL: %s statfs fsid is zero\n", name);
		return 0;
	}
	for (i = 0; i < 4; i++) {
		if (st->f_spare[i] != 0) {
			printf("FAIL: %s statfs spare[%d]=%ld\n", name, i,
			       st->f_spare[i]);
			return 0;
		}
	}

	return 1;
}

static int test_statfs_basic_fields(void)
{
	struct statfs64 path_st;
	struct statfs64 fd_st;
	long fd;
	long ret;

	memset(&path_st, 0, sizeof(path_st));
	ret = statfs64("/", &path_st);
	if (ret != 0 || !statfs_basic_fields_ok("path", &path_st)) {
		printf("FAIL: statfs64 root ret=%ld\n", ret);
		return 1;
	}

	fd = open("/", O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		printf("FAIL: open root directory got %ld\n", fd);
		return 1;
	}

	memset(&fd_st, 0, sizeof(fd_st));
	ret = fstatfs64((int)fd, &fd_st);
	close((int)fd);
	if (ret != 0 || !statfs_basic_fields_ok("fd", &fd_st)) {
		printf("FAIL: fstatfs64 root ret=%ld\n", ret);
		return 1;
	}

	if (fd_st.f_type != path_st.f_type || fd_st.f_bsize != path_st.f_bsize ||
	    fd_st.f_frsize != path_st.f_frsize ||
	    fd_st.f_namelen != path_st.f_namelen ||
	    fd_st.f_fsid[0] != path_st.f_fsid[0] ||
	    fd_st.f_fsid[1] != path_st.f_fsid[1]) {
		printf("FAIL: statfs path/fd mismatch\n");
		return 1;
	}

	return 0;
}

static int test_fallocate_mode0_allocates_blocks(void)
{
	char buf[16];
	struct stat before;
	struct stat after;
	struct statx stx;
	long fd;
	long rofd;
	long ret;
	int failed = 0;

	memset(&before, 0, sizeof(before));
	memset(&after, 0, sizeof(after));
	unlinkat(AT_FDCWD, "/tmp/fallocate_file", 0);
	fd = openat(AT_FDCWD, "/tmp/fallocate_file",
		    O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open fallocate file: %ld\n", fd);
		return 1;
	}

	ret = write((int)fd, "abc", 3);
	if (ret != 3 || fstat((int)fd, &before) != 0) {
		printf("FAIL: prepare fallocate file ret=%ld\n", ret);
		close((int)fd);
		unlinkat(AT_FDCWD, "/tmp/fallocate_file", 0);
		return 1;
	}

	ret = fallocate((int)fd, 0, 8192, 4096);
	if (ret != 0) {
		printf("FAIL: fallocate mode 0 expected 0 got %ld\n", ret);
		close((int)fd);
		unlinkat(AT_FDCWD, "/tmp/fallocate_file", 0);
		return 1;
	}

	if (fstat((int)fd, &after) != 0) {
		printf("FAIL: fstat after fallocate\n");
		failed++;
	} else {
		if (after.st_size != 12288) {
			printf("FAIL: fallocate size expected 12288 got %ld\n",
			       after.st_size);
			failed++;
		}
		if (after.st_blocks <= before.st_blocks) {
			printf("FAIL: fallocate blocks before=%lu after=%lu\n",
			       before.st_blocks, after.st_blocks);
			failed++;
		}
	}

	memset(&stx, 0, sizeof(stx));
	ret = statx((int)fd, "", AT_EMPTY_PATH, STATX_BASIC_STATS, &stx);
	if (ret != 0 || stx.stx_size != 12288 ||
	    stx.stx_blocks != after.st_blocks) {
		printf("FAIL: fallocate statx ret=%ld size=%lu blocks=%lu\n",
		       ret, stx.stx_size, stx.stx_blocks);
		failed++;
	}

	memset(buf, 0, sizeof(buf));
	lseek((int)fd, 0, SEEK_SET);
	ret = read((int)fd, buf, 3);
	if (ret != 3 || strncmp(buf, "abc", 3) != 0) {
		printf("FAIL: fallocate changed prefix ret=%ld data=%s\n", ret,
		       buf);
		failed++;
	}

	memset(buf, 0x5a, sizeof(buf));
	lseek((int)fd, 8192, SEEK_SET);
	ret = read((int)fd, buf, sizeof(buf));
	if (ret != (long)sizeof(buf)) {
		printf("FAIL: fallocate zero read ret=%ld\n", ret);
		failed++;
	} else {
		for (int i = 0; i < (int)sizeof(buf); i++) {
			if (buf[i] != 0) {
				printf("FAIL: fallocate byte[%d]=0x%x\n", i,
				       (unsigned char)buf[i]);
				failed++;
				break;
			}
		}
	}

	ret = fallocate((int)fd, FALLOC_FL_KEEP_SIZE, 0, 4096);
	if (ret != -EINVAL) {
		printf("FAIL: fallocate KEEP_SIZE expected -EINVAL got %ld\n",
		       ret);
		failed++;
	}
	ret = fallocate((int)fd, 0, -1, 4096);
	if (ret != -EINVAL) {
		printf("FAIL: fallocate negative offset got %ld\n", ret);
		failed++;
	}
	ret = fallocate((int)fd, 0, 0, 0);
	if (ret != -EINVAL) {
		printf("FAIL: fallocate zero len got %ld\n", ret);
		failed++;
	}

	rofd = openat(AT_FDCWD, "/tmp/fallocate_file", O_RDONLY, 0);
	ret = rofd >= 0 ? fallocate((int)rofd, 0, 0, 4096) : rofd;
	if (rofd >= 0)
		close((int)rofd);
	if (ret != -EBADF) {
		printf("FAIL: fallocate readonly expected -EBADF got %ld\n",
		       ret);
		failed++;
	}

	close((int)fd);
	unlinkat(AT_FDCWD, "/tmp/fallocate_file", 0);
	return failed;
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
	unsigned long mount_flags[] = {
		MS_RDONLY,
		MS_BIND,
		MS_REC,
		MS_MOVE,
		MS_REMOUNT,
		MS_PRIVATE,
		MS_SHARED,
	};
	int umount_flags[] = {
		MNT_FORCE,
		MNT_DETACH,
		MNT_EXPIRE,
		UMOUNT_NOFOLLOW,
	};

	if (mount_test_setup())
		return 1;

	for (int i = 0; i < (int)(sizeof(mount_flags) /
				  sizeof(mount_flags[0]));
	     i++) {
		ret = mount(MOUNT_DEV, MOUNT_DIR, "ext2", mount_flags[i],
			    NULL);
		if (ret != -EINVAL) {
			printf("FAIL: mount flag 0x%lx expected -EINVAL got %ld\n",
			       mount_flags[i], ret);
			failed++;
		}
	}
	for (int i = 0; i < (int)(sizeof(umount_flags) /
				  sizeof(umount_flags[0]));
	     i++) {
		ret = umount2(MOUNT_DIR, umount_flags[i]);
		if (ret != -EINVAL) {
			printf("FAIL: umount flag 0x%x expected -EINVAL got %ld\n",
			       umount_flags[i], ret);
			failed++;
		}
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
	report_group("sendfile regular to regular explicit offset",
		     test_sendfile_regular_to_regular_offset(), &failed);
	report_group("sendfile file to pipe null offset",
		     test_sendfile_file_to_pipe_null_offset(), &failed);
	report_group("sendfile file to stdout",
		     test_sendfile_file_to_stdout(), &failed);
	report_group("sendfile rejects append output",
		     test_sendfile_rejects_append_output(), &failed);
	report_group("splice pipe to file explicit offset",
		     test_splice_pipe_to_file_explicit_offset(), &failed);
	report_group("splice pipe write error preserves pipe",
		     test_splice_pipe_to_file_write_error_preserves_pipe(),
		     &failed);
	report_group("splice file to pipe explicit offset and eof",
		     test_splice_file_to_pipe_explicit_offset_and_eof(),
		     &failed);
	report_group("statx basic regular file", test_statx_basic_regular_file(),
		     &failed);
	report_group("statfs basic fields", test_statfs_basic_fields(),
		     &failed);
	report_group("fallocate mode 0 allocates blocks",
		     test_fallocate_mode0_allocates_blocks(), &failed);
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

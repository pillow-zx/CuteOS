/*
 * user/bin/fs_test.c - filesystem and pipe user ABI tests
 */

#include <ulib.h>

#define EPIPE 32

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
	report_group("pipe eof", test_pipe_eof(), &failed);
	report_group("pipe epipe", test_pipe_epipe(), &failed);

	if (failed)
		printf("fs_test: %d test group(s) FAILED\n", failed);
	else
		printf("fs_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

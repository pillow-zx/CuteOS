/*
 * user/bin/rename_test.c - renameat2 / rename 测试
 *
 * 测试内容：
 *   1. 基本文件改名：src 消失，dst 出现且内容正确
 *   2. RENAME_NOREPLACE：目标已存在时返回 -EEXIST
 *   3. 覆盖已有文件：dst 内容替换为 src
 *   4. 同路径 rename 是 no-op
 *   5. 目录不能移动到自己的子树下
 *   6. RENAME_NOREPLACE 对未预热 dcache 的既有目标返回 -EEXIST
 *   7. rename 当前工作目录后，相对路径仍基于原 cwd 引用工作
 */

#include <ulib.h>

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
			printf("  read_check: byte[%d] 0x%02x != 0x%02x\n",
			       i, (unsigned char)buf[i],
			       (unsigned char)expect[i]);
			return -1;
		}
	}
	return 0;
}

/* test 1: basic rename: src disappears, dst appears with right content */
static int test_basic(void)
{
	long ret;
	int  fd;

	if (make_file("/tmp/rn_src", "hello", 5) < 0) {
		printf("FAIL: make_file rn_src\n");
		return 1;
	}

	ret = renameat2(AT_FDCWD, "/tmp/rn_src",
			AT_FDCWD, "/tmp/rn_dst", 0);
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

	ret = renameat2(AT_FDCWD, "/tmp/nr_src",
			AT_FDCWD, "/tmp/nr_dst",
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

	ret = renameat2(AT_FDCWD, "/tmp/rp_src",
			AT_FDCWD, "/tmp/rp_dst", 0);
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

	ret = renameat2(AT_FDCWD, "/tmp/same_path",
			AT_FDCWD, "/tmp/same_path", 0);
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

	ret = renameat2(AT_FDCWD, "/tmp/rndir",
			AT_FDCWD, "/tmp/rndir/sub/moved", 0);
	if (ret != -EINVAL) {
		printf("FAIL: dir into subtree expected -EINVAL got %ld\n", ret);
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

	ret = renameat2(AT_FDCWD, "/tmp/nr_uncached_src",
			AT_FDCWD, "/bin/sigaltstack_test",
			RENAME_NOREPLACE);
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

/* test 7: an existing cwd dentry remains usable after its directory is renamed */
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

	ret = renameat2(AT_FDCWD, "/tmp/cwd_old",
			AT_FDCWD, "/tmp/cwd_new", 0);
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

int main(void)
{
	int fail = 0;

	/* Ensure /tmp exists; ignore error if it already does. */
	mkdirat(AT_FDCWD, "/tmp", 0755);

	fail += test_basic();
	fail += test_noreplace();
	fail += test_replace_existing();
	fail += test_same_path();
	fail += test_dir_into_subtree();
	fail += test_noreplace_existing_uncached_target();
	fail += test_rename_cwd_keeps_reference();

	if (fail == 0)
		printf("rename_test: PASS\n");
	else
		printf("rename_test: FAIL (%d)\n", fail);
	return fail ? 1 : 0;
}

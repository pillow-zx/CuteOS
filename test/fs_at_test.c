/*
 * test/fs_at_test.c - *at 系统调用 VFS 语义回归测试
 *
 * 覆盖：
 *   - path_lookupat_path 基本路径解析（NULL base = cwd）
 *   - 空路径、不存在路径的错误码
 *   - mkdir_at / unlink_at (AT_REMOVEDIR) 往返
 *   - vfs_readlink 对非符号链接返回 -EINVAL
 *   - LOOKUP_NOFOLLOW 对普通文件/目录无副作用
 *   - vfs_openat 目录打开和文件读写
 */

#include <kernel/errno.h>
#include <kernel/blkdev.h>
#include <kernel/fs.h>
#include <kernel/fdtable.h>
#include <kernel/statfs.h>
#include <kernel/stat.h>
#include <kernel/string.h>
#include <kernel/test.h>
#include <kernel/vfs.h>

#include "ktest.h"

#define FAT_DIR	 "/fat_testdir"
#define FAT_FILE "/fat_testfile"
#define FAT_MOUNT_DIR "/fat_mount"
#define FAT_MOUNT_DEV "/fat_mount_dev"

#define EXT2_SUPER_MAGIC 0xef53

void test_fs_at_path_lookup_basics(void)
{
	struct path path = {0};

	TEST_BEGIN("fs-at: path_lookupat_path basics");
	{
		/* Root lookup from cwd (NULL base) must succeed. */
		TEST_ASSERT_EQ(path_lookupat_path(NULL, "/", 0, &path), 0);
		TEST_ASSERT_NOT_NULL(path.mnt);
		TEST_ASSERT_NOT_NULL(path.dentry);
		path_put(&path);

		/* Known non-existent path must return -ENOENT. */
		TEST_ASSERT_EQ(
			path_lookupat_path(NULL, "/no_such_entry_xyz", 0,
					   &path),
			-ENOENT);
		TEST_ASSERT_NULL(path.dentry);

		/* Component-based lookup through root. */
		TEST_ASSERT_EQ(path_lookupat_path(NULL, "/.", 0, &path), 0);
		TEST_ASSERT_NOT_NULL(path.mnt);
		TEST_ASSERT_NOT_NULL(path.dentry);
		path_put(&path);
	}
	TEST_END("fs-at: path_lookupat_path basics");
	return;
fail:
	path_put(&path);
	TEST_FAIL("fs-at: path_lookupat_path basics", "see above");
}

void test_fs_at_empty_path_error(void)
{
	struct path path = {0};
	int ret;

	TEST_BEGIN("fs-at: empty path returns error");
	{
		/*
		 * An empty path must not succeed for ordinary lookup; the
		 * kernel returns either -ENOENT or -EINVAL.
		 */
		ret = path_lookupat_path(NULL, "", 0, &path);
		TEST_ASSERT(ret < 0);
		TEST_ASSERT_NULL(path.dentry);
	}
	TEST_END("fs-at: empty path returns error");
	return;
fail:
	TEST_FAIL("fs-at: empty path returns error", "see above");
}

void test_fs_at_mkdir_rmdir_cycle(void)
{
	struct path path = {0};
	int ret;

	/* Clean up any leftover from a previous run. */
	(void)vfs_unlink_at_path(NULL, FAT_DIR, AT_REMOVEDIR);

	TEST_BEGIN("fs-at: mkdir_at / rmdir_at cycle");
	{
		/* Create the directory. */
		ret = vfs_mkdir_at_path(NULL, FAT_DIR, 0755);
		TEST_ASSERT_EQ(ret, 0);

		/* Look it up — must exist and be a directory. */
		ret = path_lookupat_path(NULL, FAT_DIR, 0, &path);
		TEST_ASSERT_EQ(ret, 0);
		TEST_ASSERT_NOT_NULL(path.mnt);
		TEST_ASSERT_NOT_NULL(path.dentry);
		TEST_ASSERT_NOT_NULL(path.dentry->d_inode);
		TEST_ASSERT(S_ISDIR(path.dentry->d_inode->i_mode));
		path_put(&path);

		/* Remove the directory via AT_REMOVEDIR. */
		ret = vfs_unlink_at_path(NULL, FAT_DIR, AT_REMOVEDIR);
		TEST_ASSERT_EQ(ret, 0);

		/* Must be gone now. */
		ret = path_lookupat_path(NULL, FAT_DIR, 0, &path);
		TEST_ASSERT_EQ(ret, -ENOENT);
		TEST_ASSERT_NULL(path.dentry);
	}
	TEST_END("fs-at: mkdir_at / rmdir_at cycle");
	return;
fail:
	path_put(&path);
	(void)vfs_unlink_at_path(NULL, FAT_DIR, AT_REMOVEDIR);
	TEST_FAIL("fs-at: mkdir_at / rmdir_at cycle", "see above");
}

void test_fs_at_readlink_not_symlink(void)
{
	struct path path = {0};
	char buf[64];
	int ret;

	TEST_BEGIN("fs-at: readlink on non-symlink returns -EINVAL");
	{
		/* Look up the root (definitely not a symlink). */
		ret = path_lookupat_path(NULL, "/", 0, &path);
		TEST_ASSERT_EQ(ret, 0);
		TEST_ASSERT_NOT_NULL(path.dentry);

		ret = vfs_readlink(path.dentry, buf, sizeof(buf));
		TEST_ASSERT_EQ(ret, -EINVAL);

		path_put(&path);
	}
	TEST_END("fs-at: readlink on non-symlink returns -EINVAL");
	return;
fail:
	path_put(&path);
	TEST_FAIL("fs-at: readlink on non-symlink returns -EINVAL",
		  "see above");
}

void test_fs_at_lookup_nofollow_on_dir(void)
{
	struct path path = {0};

	TEST_BEGIN("fs-at: LOOKUP_NOFOLLOW on directory is harmless");
	{
		/* LOOKUP_NOFOLLOW must not affect non-symlink lookups. */
		TEST_ASSERT_EQ(
			path_lookupat_path(NULL, "/", LOOKUP_NOFOLLOW, &path),
			0);
		TEST_ASSERT_NOT_NULL(path.dentry);
		TEST_ASSERT(S_ISDIR(path.dentry->d_inode->i_mode));
		path_put(&path);
	}
	TEST_END("fs-at: LOOKUP_NOFOLLOW on directory is harmless");
	return;
fail:
	path_put(&path);
	TEST_FAIL("fs-at: LOOKUP_NOFOLLOW on directory is harmless",
		  "see above");
}

void test_fs_at_non_directory_parent_error(void)
{
	struct path path = {0};
	int ret;

	TEST_BEGIN("fs-at: non-directory parent returns -ENOTDIR");
	{
		ret = path_lookupat_path(NULL, "/bin/sh/child", 0, &path);
		TEST_ASSERT_EQ(ret, -ENOTDIR);
		TEST_ASSERT_NULL(path.dentry);
	}
	TEST_END("fs-at: non-directory parent returns -ENOTDIR");
	return;
fail:
	path_put(&path);
	TEST_FAIL("fs-at: non-directory parent returns -ENOTDIR", "see above");
}

void test_fs_at_openat_regular_file(void)
{
	const char data[] = "fat-test";
	char rbuf[16];
	int fd = -1;
	struct file *f = NULL;
	ssize_t n;

	/* Clean up any leftover. */
	(void)vfs_unlink_at_path(NULL, FAT_FILE, 0);

	TEST_BEGIN("fs-at: openat create, write, read, unlink");
	{
		fd = vfs_open(FAT_FILE, O_RDWR | O_CREAT, 0644);
		TEST_ASSERT(fd >= 0);

		f = fd_get(fd);
		TEST_ASSERT_NOT_NULL(f);

		n = vfs_write(f, data, sizeof(data) - 1);
		TEST_ASSERT_EQ((ssize_t)(sizeof(data) - 1), n);

		TEST_ASSERT(vfs_llseek(f, 0, 0 /* SEEK_SET */) >= 0);

		n = vfs_read(f, rbuf, sizeof(rbuf));
		TEST_ASSERT_EQ(n, (ssize_t)(sizeof(data) - 1));
		TEST_ASSERT_EQ(rbuf[0], 'f');

		file_put(f);
		f = NULL;
		fd_close(fd);
		fd = -1;

		TEST_ASSERT_EQ(vfs_unlink_at_path(NULL, FAT_FILE, 0), 0);

		/* Must be gone. */
		struct path path = {0};
		TEST_ASSERT_EQ(path_lookupat_path(NULL, FAT_FILE, 0, &path),
			       -ENOENT);
		TEST_ASSERT_NULL(path.dentry);
	}
	TEST_END("fs-at: openat create, write, read, unlink");
	return;
fail:
	if (f)
		file_put(f);
	if (fd >= 0)
		fd_close(fd);
	(void)vfs_unlink_at_path(NULL, FAT_FILE, 0);
	TEST_FAIL("fs-at: openat create, write, read, unlink", "see above");
}

void test_fs_mount_ext2_on_directory(void)
{
	struct path path = {0};
	struct statfs64 st;
	int ret;
	int ignored;

	ignored = vfs_umount(FAT_MOUNT_DIR, 0);
	(void)ignored;
	(void)vfs_unlink_at_path(NULL, FAT_MOUNT_DIR, AT_REMOVEDIR);
	(void)vfs_unlink_at_path(NULL, FAT_MOUNT_DEV, 0);

	TEST_BEGIN("fs-mount: mount ext2 block device on directory");
	{
		ret = vfs_mknod_at_path(NULL, FAT_MOUNT_DEV, S_IFBLK | 0600,
					MKDEV(8, 0));
		TEST_ASSERT_EQ(ret, 0);
		ret = vfs_mkdir_at_path(NULL, FAT_MOUNT_DIR, 0755);
		TEST_ASSERT_EQ(ret, 0);

		ret = vfs_mount(FAT_MOUNT_DEV, FAT_MOUNT_DIR, "ext2", 0,
				NULL);
		TEST_ASSERT_EQ(ret, 0);

		ret = path_lookupat_path(NULL, FAT_MOUNT_DIR, 0, &path);
		TEST_ASSERT_EQ(ret, 0);
		TEST_ASSERT_NOT_NULL(path.mnt);
		TEST_ASSERT_NOT_NULL(path.dentry);
		TEST_ASSERT_EQ(vfs_statfs(path.mnt->mnt_sb, &st), 0);
		TEST_ASSERT_EQ(st.f_type, EXT2_SUPER_MAGIC);
		path_put(&path);

		TEST_ASSERT_EQ(vfs_umount(FAT_MOUNT_DIR, 0), 0);
		TEST_ASSERT_EQ(vfs_unlink_at_path(NULL, FAT_MOUNT_DIR,
						  AT_REMOVEDIR),
			       0);
		TEST_ASSERT_EQ(vfs_unlink_at_path(NULL, FAT_MOUNT_DEV, 0), 0);
	}
	TEST_END("fs-mount: mount ext2 block device on directory");
	return;
fail:
	path_put(&path);
	ignored = vfs_umount(FAT_MOUNT_DIR, 0);
	(void)ignored;
	(void)vfs_unlink_at_path(NULL, FAT_MOUNT_DIR, AT_REMOVEDIR);
	(void)vfs_unlink_at_path(NULL, FAT_MOUNT_DEV, 0);
	TEST_FAIL("fs-mount: mount ext2 block device on directory",
		  "see above");
}

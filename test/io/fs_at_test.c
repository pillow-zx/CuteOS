/*
 * test/fs_at_test.c - *at 系统调用 VFS 语义回归测试
 */

#include <kernel/errno.h>
#include <kernel/blkdev.h>
#include <kernel/fs.h>
#include <kernel/fdtable.h>
#include <kernel/statfs.h>
#include <kernel/stat.h>
#include <kernel/test.h>
#include <kernel/vfs.h>

#include "../../fs/ext2/ext2.h"
#include "../ktest.h"

#define FAT_DIR	 "/fat_testdir"
#define FAT_FILE "/fat_testfile"
#define FAT_MOUNT_DIR "/fat_mount"
#define FAT_MOUNT_DEV "/fat_mount_dev"
#define FAT_LARGE_BGDT_DIR "/fat_large_bgdt_mount"
#define FAT_LARGE_BGDT_DEV "/fat_large_bgdt_dev"

#define FAT_LARGE_BGDT_GROUPS 65U
#define FAT_LARGE_BGDT_MAJOR  31U

static uint8_t fat_large_bgdt_blocks[3][BLOCK_SIZE];
static struct block_device fat_large_bgdt_bdev;

static int fat_large_bgdt_read_sectors(struct block_device *bdev, void *buf,
				       uint64_t sector, uint32_t nsec)
{
	uint32_t block = (uint32_t)(sector / BLOCK_SECTORS);
	uint32_t offset = (uint32_t)(sector % BLOCK_SECTORS) * SECTOR_SIZE;
	uint32_t bytes = nsec * SECTOR_SIZE;

	(void)bdev;

	if (!buf || sector % BLOCK_SECTORS || nsec != BLOCK_SECTORS)
		return -EINVAL;
	if (block >= ARRLEN(fat_large_bgdt_blocks))
		return -EIO;
	if (offset + bytes > BLOCK_SIZE)
		return -EIO;

	memcpy(buf, fat_large_bgdt_blocks[block] + offset, bytes);
	return 0;
}

static int fat_large_bgdt_write_sectors(struct block_device *bdev,
					const void *buf, uint64_t sector,
					uint32_t nsec)
{
	uint32_t block = (uint32_t)(sector / BLOCK_SECTORS);
	uint32_t offset = (uint32_t)(sector % BLOCK_SECTORS) * SECTOR_SIZE;
	uint32_t bytes = nsec * SECTOR_SIZE;

	(void)bdev;

	if (!buf || sector % BLOCK_SECTORS || nsec != BLOCK_SECTORS)
		return -EINVAL;
	if (block >= ARRLEN(fat_large_bgdt_blocks))
		return -EIO;
	if (offset + bytes > BLOCK_SIZE)
		return -EIO;

	memcpy(fat_large_bgdt_blocks[block] + offset, buf, bytes);
	return 0;
}

static const struct block_device_operations fat_large_bgdt_ops = {
	.read_sectors = fat_large_bgdt_read_sectors,
	.write_sectors = fat_large_bgdt_write_sectors,
};

static void fat_large_bgdt_init_image(void)
{
	struct ext2_super_block *es;
	struct ext2_group_desc *gd;
	struct ext2_inode *root;

	memset(fat_large_bgdt_blocks, 0, sizeof(fat_large_bgdt_blocks));

	es = (struct ext2_super_block *)(uintptr_t)
		(fat_large_bgdt_blocks[0] + EXT2_SUPER_OFFSET);
	es->s_inodes_count = FAT_LARGE_BGDT_GROUPS;
	es->s_blocks_count = FAT_LARGE_BGDT_GROUPS;
	es->s_free_blocks_count = FAT_LARGE_BGDT_GROUPS * 3U;
	es->s_free_inodes_count = FAT_LARGE_BGDT_GROUPS * 5U;
	es->s_first_data_block = 0;
	es->s_log_block_size = 2;
	es->s_log_frag_size = 2;
	es->s_blocks_per_group = 1;
	es->s_frags_per_group = 1;
	es->s_inodes_per_group = 8;
	es->s_magic = EXT2_SUPER_MAGIC;
	es->s_rev_level = EXT2_DYNAMIC_REV;
	es->s_inode_size = EXT2_GOOD_OLD_INODE_SIZE;
	es->s_feature_incompat = EXT2_FEATURE_INCOMPAT_FILETYPE;

	gd = (struct ext2_group_desc *)(uintptr_t)fat_large_bgdt_blocks[1];
	for (uint32_t i = 0; i < FAT_LARGE_BGDT_GROUPS; i++) {
		gd[i].bg_inode_table = 2;
		gd[i].bg_free_blocks_count = 3;
		gd[i].bg_free_inodes_count = 5;
	}

	root = (struct ext2_inode *)(uintptr_t)
		(fat_large_bgdt_blocks[2] + EXT2_GOOD_OLD_INODE_SIZE);
	root->i_mode = EXT2_S_IFDIR | 0755;
	root->i_links_count = 2;
	root->i_size = 0;
}

static int fat_large_bgdt_register_device(void)
{
	fat_large_bgdt_init_image();
	memset(&fat_large_bgdt_bdev, 0, sizeof(fat_large_bgdt_bdev));
	fat_large_bgdt_bdev.bd_dev = MKDEV(FAT_LARGE_BGDT_MAJOR, 0);
	fat_large_bgdt_bdev.bd_sectors =
		ARRLEN(fat_large_bgdt_blocks) * BLOCK_SECTORS;
	fat_large_bgdt_bdev.bd_ops = &fat_large_bgdt_ops;
	return register_block_device(&fat_large_bgdt_bdev);
}

int test_fs_at_path_lookup_basics(void)
{
	struct path path = {0};

	TEST_BEGIN("fs-at: path_lookupat_path basics");
	{

		TEST_ASSERT_EQ(path_lookupat_path(NULL, "/", 0, &path), 0);
		TEST_ASSERT_NOT_NULL(path.mnt);
		TEST_ASSERT_NOT_NULL(path.dentry);
		path_put(&path);


		TEST_ASSERT_EQ(
			path_lookupat_path(NULL, "/no_such_entry_xyz", 0,
					   &path),
			-ENOENT);
		TEST_ASSERT_NULL(path.dentry);


		TEST_ASSERT_EQ(path_lookupat_path(NULL, "/.", 0, &path), 0);
		TEST_ASSERT_NOT_NULL(path.mnt);
		TEST_ASSERT_NOT_NULL(path.dentry);
		path_put(&path);
	}
	TEST_END("fs-at: path_lookupat_path basics");
	return __test_ret;
fail:
	path_put(&path);
	TEST_FAIL("fs-at: path_lookupat_path basics", "see above");

	return __test_ret;
}

int test_fs_at_empty_path_error(void)
{
	struct path path = {0};
	int ret;

	TEST_BEGIN("fs-at: empty path returns error");
	{

		ret = path_lookupat_path(NULL, "", 0, &path);
		TEST_ASSERT(ret < 0);
		TEST_ASSERT_NULL(path.dentry);
	}
	TEST_END("fs-at: empty path returns error");
	return __test_ret;
fail:
	TEST_FAIL("fs-at: empty path returns error", "see above");

	return __test_ret;
}

int test_fs_at_mkdir_rmdir_cycle(void)
{
	struct path path = {0};
	int ret;


	(void)vfs_unlink_at_path(NULL, FAT_DIR, AT_REMOVEDIR);

	TEST_BEGIN("fs-at: mkdir_at / rmdir_at cycle");
	{

		ret = vfs_mkdir_at_path(NULL, FAT_DIR, 0755);
		TEST_ASSERT_EQ(ret, 0);


		ret = path_lookupat_path(NULL, FAT_DIR, 0, &path);
		TEST_ASSERT_EQ(ret, 0);
		TEST_ASSERT_NOT_NULL(path.mnt);
		TEST_ASSERT_NOT_NULL(path.dentry);
		TEST_ASSERT_NOT_NULL(path.dentry->d_inode);
		TEST_ASSERT(S_ISDIR(path.dentry->d_inode->i_mode));
		path_put(&path);


		ret = vfs_unlink_at_path(NULL, FAT_DIR, AT_REMOVEDIR);
		TEST_ASSERT_EQ(ret, 0);


		ret = path_lookupat_path(NULL, FAT_DIR, 0, &path);
		TEST_ASSERT_EQ(ret, -ENOENT);
		TEST_ASSERT_NULL(path.dentry);
	}
	TEST_END("fs-at: mkdir_at / rmdir_at cycle");
	return __test_ret;
fail:
	path_put(&path);
	(void)vfs_unlink_at_path(NULL, FAT_DIR, AT_REMOVEDIR);
	TEST_FAIL("fs-at: mkdir_at / rmdir_at cycle", "see above");

	return __test_ret;
}

int test_fs_at_readlink_not_symlink(void)
{
	struct path path = {0};
	char buf[64];
	int ret;

	TEST_BEGIN("fs-at: readlink on non-symlink returns -EINVAL");
	{

		ret = path_lookupat_path(NULL, "/", 0, &path);
		TEST_ASSERT_EQ(ret, 0);
		TEST_ASSERT_NOT_NULL(path.dentry);

		ret = vfs_readlink(path.dentry, buf, sizeof(buf));
		TEST_ASSERT_EQ(ret, -EINVAL);

		path_put(&path);
	}
	TEST_END("fs-at: readlink on non-symlink returns -EINVAL");
	return __test_ret;
fail:
	path_put(&path);
	TEST_FAIL("fs-at: readlink on non-symlink returns -EINVAL",
		  "see above");

	return __test_ret;
}

int test_fs_at_lookup_nofollow_on_dir(void)
{
	struct path path = {0};

	TEST_BEGIN("fs-at: LOOKUP_NOFOLLOW on directory is harmless");
	{

		TEST_ASSERT_EQ(
			path_lookupat_path(NULL, "/", LOOKUP_NOFOLLOW, &path),
			0);
		TEST_ASSERT_NOT_NULL(path.dentry);
		TEST_ASSERT(S_ISDIR(path.dentry->d_inode->i_mode));
		path_put(&path);
	}
	TEST_END("fs-at: LOOKUP_NOFOLLOW on directory is harmless");
	return __test_ret;
fail:
	path_put(&path);
	TEST_FAIL("fs-at: LOOKUP_NOFOLLOW on directory is harmless",
		  "see above");

	return __test_ret;
}

int test_fs_at_non_directory_parent_error(void)
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
	return __test_ret;
fail:
	path_put(&path);
	TEST_FAIL("fs-at: non-directory parent returns -ENOTDIR", "see above");

	return __test_ret;
}

int test_fs_at_openat_regular_file(void)
{
	const char data[] = "fat-test";
	char rbuf[16];
	int fd = -1;
	struct file *f = NULL;
	ssize_t n;


	(void)vfs_unlink_at_path(NULL, FAT_FILE, 0);

	TEST_BEGIN("fs-at: openat create, write, read, unlink");
	{
		fd = vfs_open(FAT_FILE, O_RDWR | O_CREAT, 0644);
		TEST_ASSERT(fd >= 0);

		f = fd_get(fd);
		TEST_ASSERT_NOT_NULL(f);

		n = vfs_write(f, data, sizeof(data) - 1);
		TEST_ASSERT_EQ((ssize_t)(sizeof(data) - 1), n);

		TEST_ASSERT(vfs_llseek(f, 0, 0 ) >= 0);

		n = vfs_read(f, rbuf, sizeof(rbuf));
		TEST_ASSERT_EQ(n, (ssize_t)(sizeof(data) - 1));
		TEST_ASSERT_EQ(rbuf[0], 'f');

		file_put(f);
		f = NULL;
		fd_close(fd);
		fd = -1;

		TEST_ASSERT_EQ(vfs_unlink_at_path(NULL, FAT_FILE, 0), 0);


		struct path path = {0};
		TEST_ASSERT_EQ(path_lookupat_path(NULL, FAT_FILE, 0, &path),
			       -ENOENT);
		TEST_ASSERT_NULL(path.dentry);
	}
	TEST_END("fs-at: openat create, write, read, unlink");
	return __test_ret;
fail:
	if (f)
		file_put(f);
	if (fd >= 0)
		fd_close(fd);
	(void)vfs_unlink_at_path(NULL, FAT_FILE, 0);
	TEST_FAIL("fs-at: openat create, write, read, unlink", "see above");

	return __test_ret;
}

int test_fs_mount_ext2_on_directory(void)
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
	return __test_ret;
fail:
	path_put(&path);
	ignored = vfs_umount(FAT_MOUNT_DIR, 0);
	(void)ignored;
	(void)vfs_unlink_at_path(NULL, FAT_MOUNT_DIR, AT_REMOVEDIR);
	(void)vfs_unlink_at_path(NULL, FAT_MOUNT_DEV, 0);
	TEST_FAIL("fs-mount: mount ext2 block device on directory",
		  "see above");

	return __test_ret;
}

int test_ext2_bgdt_uses_vmalloc_for_large_tables(void)
{
	struct path path = {0};
	struct statfs64 st;
	int ret;
	int ignored;

	ignored = vfs_umount(FAT_LARGE_BGDT_DIR, 0);
	(void)ignored;
	(void)vfs_unlink_at_path(NULL, FAT_LARGE_BGDT_DIR, AT_REMOVEDIR);
	(void)vfs_unlink_at_path(NULL, FAT_LARGE_BGDT_DEV, 0);

	TEST_BEGIN("ext2: large BGDT uses vmalloc");
	{
		ret = fat_large_bgdt_register_device();
		TEST_ASSERT_EQ(ret, 0);
		ret = vfs_mknod_at_path(NULL, FAT_LARGE_BGDT_DEV,
					S_IFBLK | 0600,
					MKDEV(FAT_LARGE_BGDT_MAJOR, 0));
		TEST_ASSERT_EQ(ret, 0);
		ret = vfs_mkdir_at_path(NULL, FAT_LARGE_BGDT_DIR, 0755);
		TEST_ASSERT_EQ(ret, 0);

		ret = vfs_mount(FAT_LARGE_BGDT_DEV, FAT_LARGE_BGDT_DIR,
				"ext2", 0, NULL);
		TEST_ASSERT_EQ(ret, 0);

		ret = path_lookupat_path(NULL, FAT_LARGE_BGDT_DIR, 0, &path);
		TEST_ASSERT_EQ(ret, 0);
		TEST_ASSERT_NOT_NULL(path.mnt);
		TEST_ASSERT_NOT_NULL(path.dentry);
		TEST_ASSERT_EQ(vfs_statfs(path.mnt->mnt_sb, &st), 0);
		TEST_ASSERT_EQ(st.f_type, EXT2_SUPER_MAGIC);
		TEST_ASSERT_EQ(st.f_blocks, FAT_LARGE_BGDT_GROUPS);
		TEST_ASSERT_EQ(st.f_bfree, FAT_LARGE_BGDT_GROUPS * 3U);
		TEST_ASSERT_EQ(st.f_ffree, FAT_LARGE_BGDT_GROUPS * 5U);
		path_put(&path);

		TEST_ASSERT_EQ(vfs_umount(FAT_LARGE_BGDT_DIR, 0), 0);
		TEST_ASSERT_EQ(vfs_unlink_at_path(NULL, FAT_LARGE_BGDT_DIR,
						  AT_REMOVEDIR),
			       0);
		TEST_ASSERT_EQ(vfs_unlink_at_path(NULL, FAT_LARGE_BGDT_DEV,
						  0),
			       0);
	}
	TEST_END("ext2: large BGDT uses vmalloc");
	return __test_ret;
fail:
	path_put(&path);
	ignored = vfs_umount(FAT_LARGE_BGDT_DIR, 0);
	(void)ignored;
	(void)vfs_unlink_at_path(NULL, FAT_LARGE_BGDT_DIR, AT_REMOVEDIR);
	(void)vfs_unlink_at_path(NULL, FAT_LARGE_BGDT_DEV, 0);
	TEST_FAIL("ext2: large BGDT uses vmalloc", "see above");

	return __test_ret;
}

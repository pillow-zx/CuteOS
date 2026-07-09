/*
 * test/vfs_mount_test.c - VFS mount selection tests
 */

#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <kernel/fs.h>
#include <kernel/test.h>
#include <kernel/vfs.h>

#include "ktest.h"

#define VFS_ROOT_PROBE_MAJOR 30U
#define VFS_ROOT_PROBE_DEV   MKDEV(VFS_ROOT_PROBE_MAJOR, 0)
#define VFS_MISSING_DEV      MKDEV(29U, 0)

static struct block_device vfs_root_probe_bdev;
static bool vfs_root_probe_bdev_registered;

static int vfs_root_probe_read_sectors(struct block_device *bdev, void *buf,
				       uint64_t sector, uint32_t nsec)
{
	(void)bdev;
	(void)sector;

	if (!buf)
		return -EINVAL;
	memset(buf, 0, nsec * SECTOR_SIZE);
	return 0;
}

static int vfs_root_probe_write_sectors(struct block_device *bdev,
					const void *buf, uint64_t sector,
					uint32_t nsec)
{
	(void)bdev;
	(void)buf;
	(void)sector;
	(void)nsec;
	return 0;
}

static const struct block_device_operations vfs_root_probe_bdev_ops = {
	.read_sectors = vfs_root_probe_read_sectors,
	.write_sectors = vfs_root_probe_write_sectors,
};

static int vfs_root_probe_register_bdev(void)
{
	if (vfs_root_probe_bdev_registered)
		return 0;

	memset(&vfs_root_probe_bdev, 0, sizeof(vfs_root_probe_bdev));
	vfs_root_probe_bdev.bd_dev = VFS_ROOT_PROBE_DEV;
	vfs_root_probe_bdev.bd_sectors = BLOCK_SECTORS;
	vfs_root_probe_bdev.bd_ops = &vfs_root_probe_bdev_ops;
	if (register_block_device(&vfs_root_probe_bdev) < 0)
		return -EINVAL;

	vfs_root_probe_bdev_registered = true;
	return 0;
}

static int vfs_root_probe_match(dev_t dev)
{
	(void)dev;
	return 1;
}

static int vfs_root_probe_error(dev_t dev)
{
	(void)dev;
	return -EIO;
}

static int vfs_root_probe_mount(struct file_system_type *fs_type, dev_t dev,
				const void *data, struct super_block **out_sb)
{
	(void)fs_type;
	(void)dev;
	(void)data;
	(void)out_sb;
	return -EIO;
}

static struct file_system_type vfs_root_probe_fs_a = {
	.name = "root_probe_a",
	.probe = vfs_root_probe_match,
	.mount = vfs_root_probe_mount,
};

static struct file_system_type vfs_root_probe_fs_b = {
	.name = "root_probe_b",
	.probe = vfs_root_probe_match,
	.mount = vfs_root_probe_mount,
};

static struct file_system_type vfs_root_probe_fs_error = {
	.name = "root_probe_error",
	.probe = vfs_root_probe_error,
	.mount = vfs_root_probe_mount,
};

static struct file_system_type vfs_root_probe_fs_no_probe = {
	.name = "root_probe_no_probe",
	.mount = vfs_root_probe_mount,
};

static void vfs_root_probe_unregister_tests(void)
{
	int ignored;

	ignored = unregister_filesystem(&vfs_root_probe_fs_a);
	(void)ignored;
	ignored = unregister_filesystem(&vfs_root_probe_fs_b);
	(void)ignored;
	ignored = unregister_filesystem(&vfs_root_probe_fs_error);
	(void)ignored;
	ignored = unregister_filesystem(&vfs_root_probe_fs_no_probe);
	(void)ignored;
}

void test_vfs_root_autodetect_missing_device(void)
{
	struct file_system_type *fs_type = NULL;

	TEST_BEGIN("vfs-root: missing block device returns -ENXIO");
	{
		TEST_ASSERT_EQ(vfs_test_select_rootfs(VFS_MISSING_DEV,
						      &fs_type),
			       -ENXIO);
		TEST_ASSERT_NULL(fs_type);
	}
	TEST_END("vfs-root: missing block device returns -ENXIO");
	return;
fail:
	TEST_FAIL("vfs-root: missing block device returns -ENXIO",
		  "see above");
}

void test_vfs_root_autodetect_no_match(void)
{
	struct file_system_type *fs_type = NULL;

	vfs_root_probe_unregister_tests();

	TEST_BEGIN("vfs-root: no probe match returns -ENODEV");
	{
		TEST_ASSERT_EQ(vfs_root_probe_register_bdev(), 0);
		TEST_ASSERT_EQ(vfs_test_select_rootfs(VFS_ROOT_PROBE_DEV,
						      &fs_type),
			       -ENODEV);
		TEST_ASSERT_NULL(fs_type);
	}
	TEST_END("vfs-root: no probe match returns -ENODEV");
	return;
fail:
	vfs_root_probe_unregister_tests();
	TEST_FAIL("vfs-root: no probe match returns -ENODEV", "see above");
}

void test_vfs_root_autodetect_single_match(void)
{
	struct file_system_type *fs_type = NULL;

	vfs_root_probe_unregister_tests();

	TEST_BEGIN("vfs-root: single probe match is selected");
	{
		TEST_ASSERT_EQ(vfs_root_probe_register_bdev(), 0);
		TEST_ASSERT_EQ(register_filesystem(&vfs_root_probe_fs_a), 0);
		TEST_ASSERT_EQ(vfs_test_select_rootfs(VFS_ROOT_PROBE_DEV,
						      &fs_type),
			       0);
		TEST_ASSERT_EQ(fs_type, &vfs_root_probe_fs_a);
		TEST_ASSERT_EQ(unregister_filesystem(&vfs_root_probe_fs_a), 0);
	}
	TEST_END("vfs-root: single probe match is selected");
	return;
fail:
	vfs_root_probe_unregister_tests();
	TEST_FAIL("vfs-root: single probe match is selected", "see above");
}

void test_vfs_root_autodetect_ambiguous_match(void)
{
	struct file_system_type *fs_type = NULL;

	vfs_root_probe_unregister_tests();

	TEST_BEGIN("vfs-root: multiple probe matches return -EINVAL");
	{
		TEST_ASSERT_EQ(vfs_root_probe_register_bdev(), 0);
		TEST_ASSERT_EQ(register_filesystem(&vfs_root_probe_fs_a), 0);
		TEST_ASSERT_EQ(register_filesystem(&vfs_root_probe_fs_b), 0);
		TEST_ASSERT_EQ(vfs_test_select_rootfs(VFS_ROOT_PROBE_DEV,
						      &fs_type),
			       -EINVAL);
		TEST_ASSERT_NULL(fs_type);
		vfs_root_probe_unregister_tests();
	}
	TEST_END("vfs-root: multiple probe matches return -EINVAL");
	return;
fail:
	vfs_root_probe_unregister_tests();
	TEST_FAIL("vfs-root: multiple probe matches return -EINVAL",
		  "see above");
}

void test_vfs_root_autodetect_probe_error(void)
{
	struct file_system_type *fs_type = NULL;

	vfs_root_probe_unregister_tests();

	TEST_BEGIN("vfs-root: hard probe error is preserved");
	{
		TEST_ASSERT_EQ(vfs_root_probe_register_bdev(), 0);
		TEST_ASSERT_EQ(register_filesystem(&vfs_root_probe_fs_error),
			       0);
		TEST_ASSERT_EQ(vfs_test_select_rootfs(VFS_ROOT_PROBE_DEV,
						      &fs_type),
			       -EIO);
		TEST_ASSERT_NULL(fs_type);
		TEST_ASSERT_EQ(unregister_filesystem(&vfs_root_probe_fs_error),
			       0);
	}
	TEST_END("vfs-root: hard probe error is preserved");
	return;
fail:
	vfs_root_probe_unregister_tests();
	TEST_FAIL("vfs-root: hard probe error is preserved", "see above");
}

void test_vfs_root_autodetect_skips_no_probe(void)
{
	struct file_system_type *fs_type = NULL;

	vfs_root_probe_unregister_tests();

	TEST_BEGIN("vfs-root: filesystems without probe are skipped");
	{
		TEST_ASSERT_EQ(vfs_root_probe_register_bdev(), 0);
		TEST_ASSERT_EQ(register_filesystem(&vfs_root_probe_fs_no_probe),
			       0);
		TEST_ASSERT_EQ(vfs_test_select_rootfs(VFS_ROOT_PROBE_DEV,
						      &fs_type),
			       -ENODEV);
		TEST_ASSERT_NULL(fs_type);
		TEST_ASSERT_EQ(
			unregister_filesystem(&vfs_root_probe_fs_no_probe),
			0);
	}
	TEST_END("vfs-root: filesystems without probe are skipped");
	return;
fail:
	vfs_root_probe_unregister_tests();
	TEST_FAIL("vfs-root: filesystems without probe are skipped",
		  "see above");
}

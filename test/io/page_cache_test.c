#include <drivers/virtio_blk.h>
#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <kernel/fdtable.h>
#include <kernel/page_cache.h>
#include <kernel/test.h>
#include <kernel/vfs.h>

#include "../../fs/ext2/ext2.h"
#include "../ktest.h"

static void fill_pattern(uint8_t *buf, size_t len, uint8_t seed)
{
	for (size_t i = 0; i < len; i++)
		buf[i] = (uint8_t)(seed + (uint8_t)(i * 13u));
}

static int open_test_file(const char *path, uint32_t flags, struct file **file)
{
	int fd = vfs_open(path, flags, 0644);

	if (fd < 0)
		return fd;

	*file = fd_get(fd);
	if (!*file) {
		fd_close(fd);
		return -EIO;
	}

	return fd;
}

static void close_test_file(int fd, struct file *file)
{
	if (file)
		file_put(file);
	if (fd >= 0)
		fd_close(fd);
}

static void unlink_test_path(const char *path)
{
	(void)vfs_unlink_at_path(NULL, path, 0);
}

static int datasync_test_writebacks;
static int datasync_test_hooks;

static int datasync_test_write_inode(struct inode *inode)
{
	(void)inode;
	datasync_test_writebacks++;
	return 0;
}

static int datasync_test_datasync_inode(struct inode *inode)
{
	(void)inode;
	datasync_test_hooks++;
	return 0;
}

static const struct super_operations datasync_fallback_sops = {
	.write_inode = datasync_test_write_inode,
};

static const struct super_operations datasync_hook_sops = {
	.write_inode = datasync_test_write_inode,
	.datasync_inode = datasync_test_datasync_inode,
};

static int read_raw_file_page(struct file *file, uint32_t index, uint8_t *buf)
{
	struct block_device *bdev;
	uint32_t pblock = 0;
	int ret;

	if (!file || !file->f_inode || !buf)
		return -EINVAL;

	ret = ext2_bmap(file->f_inode, index, false, &pblock);
	if (ret < 0)
		return ret;
	if (!pblock)
		return -ENOENT;

	bdev = lookup_block_device(file->f_inode->i_sb->s_dev);
	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->read_sectors)
		return -ENXIO;

	return bdev->bd_ops->read_sectors(bdev, buf, pblock * BLOCK_SECTORS,
					  BLOCK_SECTORS);
}

static int read_raw_inode(struct inode *inode, struct ext2_inode *raw)
{
	static uint8_t block_buf[BLOCK_SIZE];
	struct ext2_sb_info *sbi;
	struct block_device *bdev;
	uint32_t ino;
	uint32_t group;
	uint32_t index;
	uint32_t byte_offset;
	uint32_t block;
	uint32_t offset;
	int ret;

	if (!inode || !inode->i_sb || !raw)
		return -EINVAL;

	sbi = EXT2_SB(inode->i_sb);
	ino = (uint32_t)inode->i_ino;
	if (!sbi || ino == 0)
		return -EINVAL;

	group = (ino - 1) / sbi->s_inodes_per_group;
	index = (ino - 1) % sbi->s_inodes_per_group;
	if (group >= sbi->s_groups_count)
		return -EINVAL;

	byte_offset = index * sbi->s_inode_size;
	block = sbi->s_group_desc[group].bg_inode_table +
		byte_offset / BLOCK_SIZE;
	offset = byte_offset % BLOCK_SIZE;
	if (offset + sizeof(*raw) > BLOCK_SIZE)
		return -EIO;

	bdev = lookup_block_device(inode->i_sb->s_dev);
	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->read_sectors)
		return -ENXIO;

	ret = bdev->bd_ops->read_sectors(bdev, block_buf, block * BLOCK_SECTORS,
					 BLOCK_SECTORS);
	if (ret < 0)
		return ret;

	memcpy(raw, block_buf + offset, sizeof(*raw));
	return 0;
}

static int read_block_alias(struct file *file, uint32_t index, uint8_t *buf)
{
	struct page_cache *page;
	uint32_t pblock = 0;
	int ret;

	if (!file || !file->f_inode || !buf)
		return -EINVAL;

	ret = ext2_bmap(file->f_inode, index, false, &pblock);
	if (ret < 0)
		return ret;
	if (!pblock)
		return -ENOENT;


	page = page_cache_get_block(file->f_inode->i_sb->s_dev, pblock);
	if (!page)
		return -EIO;

	memcpy(buf, page_cache_data(page), BLOCK_SIZE);
	page_cache_put_page(page);
	return 0;
}

static int write_raw_file_page(struct file *file, uint32_t index,
			       const uint8_t *buf)
{
	struct block_device *bdev;
	uint32_t pblock = 0;
	int ret;

	if (!file || !file->f_inode || !buf)
		return -EINVAL;

	ret = ext2_bmap(file->f_inode, index, false, &pblock);
	if (ret < 0)
		return ret;
	if (!pblock)
		return -ENOENT;

	bdev = lookup_block_device(file->f_inode->i_sb->s_dev);
	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->write_sectors)
		return -ENXIO;

	return bdev->bd_ops->write_sectors(bdev, buf, pblock * BLOCK_SECTORS,
					   BLOCK_SECTORS);
}

static bool dir_page_has_entry(const uint8_t *data, const char *name)
{
	size_t namelen = strlen(name);
	uint32_t offset = 0;

	while (offset + 8 <= BLOCK_SIZE) {
		const struct ext2_dir_entry_2 *de =
			(const struct ext2_dir_entry_2 *)(data + offset);

		if (de->rec_len < 8 || offset + de->rec_len > BLOCK_SIZE)
			break;
		if (de->inode && de->name_len == namelen &&
		    memcmp(de->name, name, namelen) == 0)
			return true;
		offset += de->rec_len;
	}

	return false;
}

int test_page_cache_dirty_write_visibility(void)
{
	static uint8_t wbuf[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct file *file = NULL;
	int fd = -1;

	TEST_BEGIN("page cache: dirty write stays off disk");
	{
		unlink_test_path("/pcache-dirty");
		fill_pattern(wbuf, sizeof(wbuf), 0x31);
		memset(raw, 0, sizeof(raw));

		fd = open_test_file("/pcache-dirty", O_CREAT | O_TRUNC | O_RDWR,
				    &file);
		TEST_ASSERT(fd >= 0);
		TEST_ASSERT_EQ(
			vfs_write(file, (const char *)wbuf, sizeof(wbuf)),
			(ssize_t)sizeof(wbuf));
		TEST_ASSERT_EQ(read_raw_file_page(file, 0, raw), 0);
		TEST_ASSERT_NE(memcmp(raw, wbuf, sizeof(wbuf)), 0);
	}
	TEST_END("page cache: dirty write stays off disk");
	goto cleanup;
fail:
	TEST_FAIL("page cache: dirty write stays off disk", "see above");
cleanup:
	close_test_file(fd, file);
	unlink_test_path("/pcache-dirty");

	return __test_ret;
}

int test_page_cache_fsync_inode_scope(void)
{
	static uint8_t abuf[BLOCK_SIZE];
	static uint8_t bbuf[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct file *file_a = NULL;
	struct file *file_b = NULL;
	int fd_a = -1;
	int fd_b = -1;

	TEST_BEGIN("page cache: fsync flushes one inode");
	{
		unlink_test_path("/pcache-fsync-a");
		unlink_test_path("/pcache-fsync-b");
		fill_pattern(abuf, sizeof(abuf), 0x51);
		fill_pattern(bbuf, sizeof(bbuf), 0x91);

		fd_a = open_test_file("/pcache-fsync-a",
				      O_CREAT | O_TRUNC | O_RDWR, &file_a);
		fd_b = open_test_file("/pcache-fsync-b",
				      O_CREAT | O_TRUNC | O_RDWR, &file_b);
		TEST_ASSERT(fd_a >= 0);
		TEST_ASSERT(fd_b >= 0);

		TEST_ASSERT_EQ(
			vfs_write(file_a, (const char *)abuf, sizeof(abuf)),
			(ssize_t)sizeof(abuf));
		TEST_ASSERT_EQ(
			vfs_write(file_b, (const char *)bbuf, sizeof(bbuf)),
			(ssize_t)sizeof(bbuf));

		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(read_raw_file_page(file_a, 0, raw), 0);
		TEST_ASSERT_NE(memcmp(raw, abuf, sizeof(abuf)), 0);
		TEST_ASSERT_EQ(read_raw_file_page(file_b, 0, raw), 0);
		TEST_ASSERT_NE(memcmp(raw, bbuf, sizeof(bbuf)), 0);

		TEST_ASSERT_EQ(vfs_sync_file(file_a), 0);

		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(read_raw_file_page(file_a, 0, raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, abuf, sizeof(abuf)), 0);
		TEST_ASSERT_EQ(read_raw_file_page(file_b, 0, raw), 0);
		TEST_ASSERT_NE(memcmp(raw, bbuf, sizeof(bbuf)), 0);
	}
	TEST_END("page cache: fsync flushes one inode");
	goto cleanup;
fail:
	TEST_FAIL("page cache: fsync flushes one inode", "see above");
cleanup:
	close_test_file(fd_a, file_a);
	close_test_file(fd_b, file_b);
	unlink_test_path("/pcache-fsync-a");
	unlink_test_path("/pcache-fsync-b");

	return __test_ret;
}

int test_vfs_datasync_metadata_policy(void)
{
	struct super_block sb = {0};
	struct inode inode = {0};
	struct file file = {0};

	TEST_BEGIN("vfs: fdatasync metadata hook policy");
	{
		inode.i_sb = &sb;
		file.f_inode = &inode;
		page_mapping_init(&inode.i_pages, &inode, NULL, NULL);

		sb.s_op = &datasync_fallback_sops;
		datasync_test_writebacks = 0;
		datasync_test_hooks = 0;
		TEST_ASSERT_EQ(vfs_datasync_file(&file), 0);
		TEST_ASSERT_EQ(datasync_test_writebacks, 1);
		TEST_ASSERT_EQ(datasync_test_hooks, 0);

		sb.s_op = &datasync_hook_sops;
		datasync_test_writebacks = 0;
		datasync_test_hooks = 0;
		TEST_ASSERT_EQ(vfs_datasync_file(&file), 0);
		TEST_ASSERT_EQ(datasync_test_writebacks, 0);
		TEST_ASSERT_EQ(datasync_test_hooks, 1);
	}
	TEST_END("vfs: fdatasync metadata hook policy");
	return __test_ret;
fail:
	TEST_FAIL("vfs: fdatasync metadata hook policy", "see above");

	return __test_ret;
}

int test_page_cache_datasync_skips_pure_inode_metadata(void)
{
	static uint8_t wbuf[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct ext2_inode before;
	struct ext2_inode after_datasync;
	struct ext2_inode after_fsync;
	struct file *file = NULL;
	uint32_t changed_atime;
	int fd = -1;

	TEST_BEGIN("page cache: fdatasync skips pure inode metadata");
	{
		unlink_test_path("/pcache-datasync-data-only");
		fill_pattern(wbuf, sizeof(wbuf), 0xb5);
		memset(raw, 0, sizeof(raw));
		memset(&before, 0, sizeof(before));
		memset(&after_datasync, 0, sizeof(after_datasync));
		memset(&after_fsync, 0, sizeof(after_fsync));

		fd = open_test_file("/pcache-datasync-data-only",
				    O_CREAT | O_TRUNC | O_RDWR, &file);
		TEST_ASSERT(fd >= 0);
		TEST_ASSERT_EQ(
			vfs_write(file, (const char *)wbuf, sizeof(wbuf)),
			(ssize_t)sizeof(wbuf));
		TEST_ASSERT_EQ(read_raw_file_page(file, 0, raw), 0);
		TEST_ASSERT_NE(memcmp(raw, wbuf, sizeof(wbuf)), 0);
		TEST_ASSERT_EQ(read_raw_inode(file->f_inode, &before), 0);

		changed_atime = before.i_atime + 1;
		if (before.i_atime == UINT32_MAX)
			changed_atime = before.i_atime - 1;
		file->f_inode->i_atime_sec = changed_atime;

		TEST_ASSERT_EQ(vfs_datasync_file(file), 0);
		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(read_raw_file_page(file, 0, raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, wbuf, sizeof(wbuf)), 0);
		TEST_ASSERT_EQ(read_raw_inode(file->f_inode, &after_datasync),
			       0);
		TEST_ASSERT_EQ(after_datasync.i_atime, before.i_atime);

		TEST_ASSERT_EQ(vfs_sync_file(file), 0);
		TEST_ASSERT_EQ(read_raw_inode(file->f_inode, &after_fsync), 0);
		TEST_ASSERT_EQ(after_fsync.i_atime, changed_atime);
	}
	TEST_END("page cache: fdatasync skips pure inode metadata");
	goto cleanup;
fail:
	TEST_FAIL("page cache: fdatasync skips pure inode metadata",
		  "see above");
cleanup:
	close_test_file(fd, file);
	unlink_test_path("/pcache-datasync-data-only");

	return __test_ret;
}

int test_page_cache_raw_alias_fsync(void)
{
	static uint8_t wbuf[BLOCK_SIZE];
	static uint8_t cached[BLOCK_SIZE];
	struct file *file = NULL;
	int fd = -1;

	TEST_BEGIN("page cache: block mapping refreshed after fsync");
	{
		unlink_test_path("/pcache-block-mapping-refresh");
		fill_pattern(wbuf, sizeof(wbuf), 0x73);
		memset(cached, 0, sizeof(cached));

		fd = open_test_file("/pcache-block-mapping-refresh",
				    O_CREAT | O_TRUNC | O_RDWR, &file);
		TEST_ASSERT(fd >= 0);
		TEST_ASSERT_EQ(
			vfs_write(file, (const char *)wbuf, sizeof(wbuf)),
			(ssize_t)sizeof(wbuf));
		TEST_ASSERT_EQ(read_block_alias(file, 0, cached), 0);
		TEST_ASSERT_NE(memcmp(cached, wbuf, sizeof(wbuf)), 0);


		TEST_ASSERT_EQ(vfs_sync_file(file), 0);

		memset(cached, 0, sizeof(cached));
		TEST_ASSERT_EQ(read_block_alias(file, 0, cached), 0);
		TEST_ASSERT_EQ(memcmp(cached, wbuf, sizeof(wbuf)), 0);
	}
	TEST_END("page cache: block mapping refreshed after fsync");
	goto cleanup;
fail:
	TEST_FAIL("page cache: block mapping refreshed after fsync",
		  "see above");
cleanup:
	close_test_file(fd, file);
	unlink_test_path("/pcache-block-mapping-refresh");

	return __test_ret;
}

int test_page_cache_directory_alias_refresh(void)
{
	static uint8_t cached[BLOCK_SIZE];
	struct path dir_path = {0};
	struct path file_path = {0};
	struct page_cache *raw_page = NULL;
	uint32_t pblock = 0;
	int ret;

	TEST_BEGIN("page cache: directory alias refresh after create");
	{
		(void)vfs_unlink_at_path(NULL, "/pcache-alias-dir/child", 0);
		(void)vfs_unlink_at_path(NULL, "/pcache-alias-dir",
					  AT_REMOVEDIR);

		TEST_ASSERT_EQ(
			vfs_mkdir_at_path(NULL, "/pcache-alias-dir", 0755), 0);
		TEST_ASSERT_EQ(path_lookupat_path(NULL, "/pcache-alias-dir", 0,
						  &dir_path),
			       0);
		TEST_ASSERT_NOT_NULL(dir_path.dentry);
		TEST_ASSERT_NOT_NULL(dir_path.dentry->d_inode);

		ret = ext2_bmap(dir_path.dentry->d_inode, 0, false, &pblock);
		TEST_ASSERT(ret >= 0);
		TEST_ASSERT_NE(pblock, 0u);
		raw_page = page_cache_get_block(
			dir_path.dentry->d_inode->i_sb->s_dev, pblock);
		TEST_ASSERT_NOT_NULL(raw_page);
		TEST_ASSERT(!dir_page_has_entry(page_cache_data(raw_page),
						"child"));

		ret = vfs_create_at_path(NULL, "/pcache-alias-dir/child", 0644,
					 &file_path);
		TEST_ASSERT_EQ(ret, 0);
		TEST_ASSERT_NOT_NULL(file_path.dentry);
		path_put(&file_path);

		memset(cached, 0, sizeof(cached));
		memcpy(cached, page_cache_data(raw_page), BLOCK_SIZE);
		TEST_ASSERT(dir_page_has_entry(cached, "child"));
	}
	TEST_END("page cache: directory alias refresh after create");
	goto cleanup;
fail:
	TEST_FAIL("page cache: directory alias refresh after create",
		  "see above");
cleanup:
	if (raw_page)
		page_cache_put_page(raw_page);
	path_put(&file_path);
	path_put(&dir_path);
	(void)vfs_unlink_at_path(NULL, "/pcache-alias-dir/child", 0);
	(void)vfs_unlink_at_path(NULL, "/pcache-alias-dir", AT_REMOVEDIR);

	return __test_ret;
}

int test_page_cache_raw_alias_drop(void)
{
	static uint8_t old_buf[BLOCK_SIZE];
	static uint8_t new_buf[BLOCK_SIZE];
	static uint8_t cached[BLOCK_SIZE];
	struct file *file = NULL;
	int fd = -1;

	TEST_BEGIN("page cache: alias invalidate reloads raw block");
	{
		unlink_test_path("/pcache-alias-invalidate");
		fill_pattern(old_buf, sizeof(old_buf), 0x19);
		fill_pattern(new_buf, sizeof(new_buf), 0xe3);

		fd = open_test_file("/pcache-alias-invalidate",
				    O_CREAT | O_TRUNC | O_RDWR, &file);
		TEST_ASSERT(fd >= 0);
		TEST_ASSERT_EQ(
			vfs_write(file, (const char *)old_buf, sizeof(old_buf)),
			(ssize_t)sizeof(old_buf));
		TEST_ASSERT_EQ(vfs_sync_file(file), 0);
		TEST_ASSERT_EQ(read_block_alias(file, 0, cached), 0);
		TEST_ASSERT_EQ(memcmp(cached, old_buf, sizeof(cached)), 0);

		TEST_ASSERT_EQ(write_raw_file_page(file, 0, new_buf), 0);
		page_cache_invalidate_inode(file->f_inode);

		memset(cached, 0, sizeof(cached));
		TEST_ASSERT_EQ(read_block_alias(file, 0, cached), 0);
		TEST_ASSERT_EQ(memcmp(cached, new_buf, sizeof(cached)), 0);
	}
	TEST_END("page cache: alias invalidate reloads raw block");
	goto cleanup;
fail:
	TEST_FAIL("page cache: alias invalidate reloads raw block",
		  "see above");
cleanup:
	close_test_file(fd, file);
	unlink_test_path("/pcache-alias-invalidate");

	return __test_ret;
}

int test_page_cache_pressure_eviction(void)
{
	enum { NR_PRESSURE_PAGES = 513 };
	static uint8_t page_buf[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct file *dirty_file = NULL;
	struct file *clean_file = NULL;
	int dirty_fd = -1;
	int clean_fd = -1;

	TEST_BEGIN("page cache: pressure eviction and progress");
	{
		unlink_test_path("/pcache-pressure");
		unlink_test_path("/pcache-clean");

		dirty_fd =
			open_test_file("/pcache-pressure",
				       O_CREAT | O_TRUNC | O_RDWR, &dirty_file);
		TEST_ASSERT(dirty_fd >= 0);

		for (uint32_t i = 0; i < NR_PRESSURE_PAGES; i++) {
			fill_pattern(page_buf, sizeof(page_buf), (uint8_t)i);
			TEST_ASSERT_EQ(vfs_write(dirty_file,
						 (const char *)page_buf,
						 sizeof(page_buf)),
				       (ssize_t)sizeof(page_buf));
		}

		fill_pattern(page_buf, sizeof(page_buf), 0);
		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(read_raw_file_page(dirty_file, 0, raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, page_buf, sizeof(page_buf)), 0);

		TEST_ASSERT_EQ(vfs_sync_file(dirty_file), 0);

		clean_fd =
			open_test_file("/pcache-clean",
				       O_CREAT | O_TRUNC | O_RDWR, &clean_file);
		TEST_ASSERT(clean_fd >= 0);
		fill_pattern(page_buf, sizeof(page_buf), 0xa7);
		TEST_ASSERT_EQ(vfs_write(clean_file, (const char *)page_buf,
					 sizeof(page_buf)),
			       (ssize_t)sizeof(page_buf));
		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(read_raw_file_page(clean_file, 0, raw), 0);
		TEST_ASSERT_NE(memcmp(raw, page_buf, sizeof(page_buf)), 0);
	}
	TEST_END("page cache: pressure eviction and progress");
	goto cleanup;
fail:
	TEST_FAIL("page cache: pressure eviction and progress", "see above");
cleanup:
	close_test_file(clean_fd, clean_file);
	close_test_file(dirty_fd, dirty_file);
	unlink_test_path("/pcache-clean");
	unlink_test_path("/pcache-pressure");

	return __test_ret;
}

int test_page_cache_clustered_writeback(void)
{
	static uint8_t page_buf[BLOCK_SIZE];
	struct virtio_blk_test_stats stats;
	uint32_t pblocks[3];
	bool contiguous = true;
	struct file *file = NULL;
	int fd = -1;
	int ret;

	TEST_BEGIN("page cache: clustered writeback");
	{
		unlink_test_path("/pcache-cluster");
		fd = open_test_file("/pcache-cluster",
				    O_CREAT | O_TRUNC | O_RDWR, &file);
		TEST_ASSERT(fd >= 0);

		for (uint32_t i = 0; i < 3; i++) {
			fill_pattern(page_buf, sizeof(page_buf),
				     (uint8_t)(0xc0 + i));
			TEST_ASSERT_EQ(vfs_write(file, (const char *)page_buf,
						 sizeof(page_buf)),
				       (ssize_t)sizeof(page_buf));
		}
		for (uint32_t i = 0; i < 3; i++) {
			ret = ext2_bmap(file->f_inode, i, false, &pblocks[i]);
			TEST_ASSERT(ret >= 0);
			TEST_ASSERT_NE(pblocks[i], 0u);
			if (i > 0 && pblocks[i] != pblocks[i - 1] + 1)
				contiguous = false;
		}

		virtio_blk_test_reset_stats();
		TEST_ASSERT_EQ(vfs_sync_file(file), 0);
		memset(&stats, 0, sizeof(stats));
		virtio_blk_test_get_stats(&stats);
		TEST_ASSERT(stats.write_reqs >= 1);
		if (contiguous)
			TEST_ASSERT(stats.max_write_nsec >= 3 * BLOCK_SECTORS);
	}
	TEST_END("page cache: clustered writeback");
	goto cleanup;
fail:
	TEST_FAIL("page cache: clustered writeback", "see above");
cleanup:
	close_test_file(fd, file);
	unlink_test_path("/pcache-cluster");

	return __test_ret;
}

int test_page_cache_indirect_reclaim_progress(void)
{
	enum {
		NR_INDIRECT_PAGES = 513,
		START_INDEX = 12,
	};
	static uint8_t page_buf[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct file *file = NULL;
	int fd = -1;

	TEST_BEGIN("page cache: indirect reclaim progress");
	{
		unlink_test_path("/pcache-indirect-reclaim");
		fd = open_test_file("/pcache-indirect-reclaim",
				    O_CREAT | O_TRUNC | O_RDWR, &file);
		TEST_ASSERT(fd >= 0);

		file->f_pos = (loff_t)START_INDEX * BLOCK_SIZE;
		for (uint32_t i = 0; i < NR_INDIRECT_PAGES; i++) {
			fill_pattern(page_buf, sizeof(page_buf),
				     (uint8_t)(0x20 + i));
			TEST_ASSERT_EQ(vfs_write(file, (const char *)page_buf,
						 sizeof(page_buf)),
				       (ssize_t)sizeof(page_buf));
		}

		TEST_ASSERT_EQ(vfs_sync_file(file), 0);

		fill_pattern(page_buf, sizeof(page_buf), 0x20);
		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(read_raw_file_page(file, START_INDEX, raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, page_buf, sizeof(page_buf)), 0);
	}
	TEST_END("page cache: indirect reclaim progress");
	goto cleanup;
fail:
	TEST_FAIL("page cache: indirect reclaim progress", "see above");
cleanup:
	close_test_file(fd, file);
	unlink_test_path("/pcache-indirect-reclaim");

	return __test_ret;
}

int test_page_cache_truncate_extend_zero_fill(void)
{
	enum {
		INITIAL_LEN = 5000,
		EXTENDED_LEN = 10000,
		READ_OFF = 4800,
		READ_LEN = 2800,
		TAIL_INDEX = 1,
	};
	static uint8_t initial[INITIAL_LEN];
	static uint8_t raw[BLOCK_SIZE];
	static uint8_t got[READ_LEN];
	static uint8_t want[READ_LEN];
	struct file *file = NULL;
	int fd = -1;

	TEST_BEGIN("page cache: truncate extend zero-fills tail");
	{
		unlink_test_path("/pcache-extend-tail");
		fill_pattern(initial, sizeof(initial), 0x44);
		memset(raw, 0, sizeof(raw));
		memset(got, 0, sizeof(got));
		memset(want, 0, sizeof(want));

		fd = open_test_file("/pcache-extend-tail",
				    O_CREAT | O_TRUNC | O_RDWR, &file);
		TEST_ASSERT(fd >= 0);
		TEST_ASSERT_EQ(
			vfs_write(file, (const char *)initial, sizeof(initial)),
			(ssize_t)sizeof(initial));
		TEST_ASSERT_EQ(vfs_sync_file(file), 0);
		TEST_ASSERT_EQ(read_raw_file_page(file, TAIL_INDEX, raw), 0);

		for (uint32_t i = INITIAL_LEN - BLOCK_SIZE; i < BLOCK_SIZE; i++)
			raw[i] = 0xa5;
		TEST_ASSERT_EQ(write_raw_file_page(file, TAIL_INDEX, raw), 0);

		page_cache_invalidate_inode(file->f_inode);
		TEST_ASSERT_EQ(vfs_truncate_file(file, EXTENDED_LEN), 0);
		TEST_ASSERT_EQ(vfs_llseek(file, READ_OFF, 0), (loff_t)READ_OFF);
		TEST_ASSERT_EQ(vfs_read(file, (char *)got, sizeof(got)),
			       (ssize_t)sizeof(got));

		memcpy(want, initial + READ_OFF, INITIAL_LEN - READ_OFF);
		TEST_ASSERT_EQ(memcmp(got, want, sizeof(got)), 0);

		TEST_ASSERT_EQ(vfs_sync_file(file), 0);
		page_cache_invalidate_inode(file->f_inode);
		memset(got, 0, sizeof(got));
		TEST_ASSERT_EQ(vfs_llseek(file, READ_OFF, 0), (loff_t)READ_OFF);
		TEST_ASSERT_EQ(vfs_read(file, (char *)got, sizeof(got)),
			       (ssize_t)sizeof(got));
		TEST_ASSERT_EQ(memcmp(got, want, sizeof(got)), 0);
	}
	TEST_END("page cache: truncate extend zero-fills tail");
	goto cleanup;
fail:
	TEST_FAIL("page cache: truncate extend zero-fills tail", "see above");
cleanup:
	close_test_file(fd, file);
	unlink_test_path("/pcache-extend-tail");

	return __test_ret;
}

int test_page_cache_large_offset_rejected(void)
{
	static uint8_t byte = 0x5a;
	struct file *file = NULL;
	int fd = -1;

	TEST_BEGIN("page cache: large offset rejected");
	{
		unlink_test_path("/pcache-large-offset");

		fd = open_test_file("/pcache-large-offset",
				    O_CREAT | O_TRUNC | O_RDWR, &file);
		TEST_ASSERT(fd >= 0);

		file->f_pos = (loff_t)UINT32_MAX + 1;
		TEST_ASSERT_EQ(vfs_write(file, (const char *)&byte, 1),
			       (ssize_t)-EFBIG);
		TEST_ASSERT_EQ(file->f_inode->i_size, 0ULL);
	}
	TEST_END("page cache: large offset rejected");
	goto cleanup;
fail:
	TEST_FAIL("page cache: large offset rejected", "see above");
cleanup:
	close_test_file(fd, file);
	unlink_test_path("/pcache-large-offset");

	return __test_ret;
}

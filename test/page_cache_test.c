#include <drivers/virtio_blk.h>
#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <kernel/fdtable.h>
#include <kernel/page_cache.h>
#include <kernel/string.h>
#include <kernel/test.h>
#include <kernel/vfs.h>

#include "../fs/ext2/ext2.h"
#include "ktest.h"

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
	(void)vfs_unlink(path, 0);
}

static int read_raw_file_page(struct file *file, uint32_t index, uint8_t *buf)
{
	struct block_device *bdev;
	uint32_t pblock;

	if (!file || !file->f_inode || !buf)
		return -EINVAL;

	pblock = ext2_bmap(file->f_inode, index, false);
	if (!pblock)
		return -ENOENT;

	bdev = lookup_block_device(file->f_inode->i_sb->s_dev);
	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->read_sectors)
		return -ENXIO;

	return bdev->bd_ops->read_sectors(bdev, buf, pblock * BLOCK_SECTORS,
					  BLOCK_SECTORS);
}

static int read_block_mapping_file_page(struct file *file, uint32_t index,
					uint8_t *buf)
{
	struct page_cache *page;
	uint32_t pblock;

	if (!file || !file->f_inode || !buf)
		return -EINVAL;

	pblock = ext2_bmap(file->f_inode, index, false);
	if (!pblock)
		return -ENOENT;

	/*
	 * Deliberately read through the block-device mapping, not the inode
	 * mapping.  The fsync test below uses this to prove file writeback
	 * refreshes an already cached raw-block alias.
	 */
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
	uint32_t pblock;

	if (!file || !file->f_inode || !buf)
		return -EINVAL;

	pblock = ext2_bmap(file->f_inode, index, false);
	if (!pblock)
		return -ENOENT;

	bdev = lookup_block_device(file->f_inode->i_sb->s_dev);
	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->write_sectors)
		return -ENXIO;

	return bdev->bd_ops->write_sectors(bdev, buf, pblock * BLOCK_SECTORS,
					   BLOCK_SECTORS);
}

void test_page_cache_dirty_write_visibility(void)
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
		TEST_ASSERT_EQ(vfs_write(file, (const char *)wbuf, sizeof(wbuf)),
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
}

void test_page_cache_fsync_inode_scope(void)
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

		TEST_ASSERT_EQ(vfs_write(file_a, (const char *)abuf, sizeof(abuf)),
			       (ssize_t)sizeof(abuf));
		TEST_ASSERT_EQ(vfs_write(file_b, (const char *)bbuf, sizeof(bbuf)),
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
}

void test_page_cache_block_mapping_refreshed_after_fsync(void)
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
		TEST_ASSERT_EQ(vfs_write(file, (const char *)wbuf, sizeof(wbuf)),
			       (ssize_t)sizeof(wbuf));
		TEST_ASSERT_EQ(read_block_mapping_file_page(file, 0, cached),
			       0);
		TEST_ASSERT_NE(memcmp(cached, wbuf, sizeof(wbuf)), 0);

		/*
		 * fsync writes the inode mapping and then refreshes the cached
		 * block-device alias for the same physical block.
		 */
		TEST_ASSERT_EQ(vfs_sync_file(file), 0);

		memset(cached, 0, sizeof(cached));
		TEST_ASSERT_EQ(read_block_mapping_file_page(file, 0, cached),
			       0);
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
}

void test_page_cache_pressure_eviction(void)
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

		dirty_fd = open_test_file("/pcache-pressure",
					  O_CREAT | O_TRUNC | O_RDWR,
					  &dirty_file);
		TEST_ASSERT(dirty_fd >= 0);

		for (uint32_t i = 0; i < NR_PRESSURE_PAGES; i++) {
			fill_pattern(page_buf, sizeof(page_buf), (uint8_t)i);
			TEST_ASSERT_EQ(
				vfs_write(dirty_file, (const char *)page_buf,
					  sizeof(page_buf)),
				(ssize_t)sizeof(page_buf));
		}

		fill_pattern(page_buf, sizeof(page_buf), 0);
		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(read_raw_file_page(dirty_file, 0, raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, page_buf, sizeof(page_buf)), 0);

		TEST_ASSERT_EQ(vfs_sync_file(dirty_file), 0);

		clean_fd = open_test_file("/pcache-clean",
					  O_CREAT | O_TRUNC | O_RDWR,
					  &clean_file);
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
}

void test_page_cache_clustered_writeback(void)
{
	static uint8_t page_buf[BLOCK_SIZE];
	struct virtio_blk_test_stats stats;
	struct file *file = NULL;
	int fd = -1;

	TEST_BEGIN("page cache: clustered writeback");
	{
		unlink_test_path("/pcache-cluster");
		fd = open_test_file("/pcache-cluster", O_CREAT | O_TRUNC | O_RDWR,
				    &file);
		TEST_ASSERT(fd >= 0);

		for (uint32_t i = 0; i < 3; i++) {
			fill_pattern(page_buf, sizeof(page_buf),
				     (uint8_t)(0xc0 + i));
			TEST_ASSERT_EQ(vfs_write(file, (const char *)page_buf,
						 sizeof(page_buf)),
				       (ssize_t)sizeof(page_buf));
		}

		virtio_blk_test_reset_stats();
		TEST_ASSERT_EQ(vfs_sync_file(file), 0);
		memset(&stats, 0, sizeof(stats));
		virtio_blk_test_get_stats(&stats);
		TEST_ASSERT(stats.write_reqs >= 1);
		TEST_ASSERT(stats.max_write_nsec >= 3 * BLOCK_SECTORS);
	}
	TEST_END("page cache: clustered writeback");
	goto cleanup;
fail:
	TEST_FAIL("page cache: clustered writeback", "see above");
cleanup:
	close_test_file(fd, file);
	unlink_test_path("/pcache-cluster");
}

void test_page_cache_indirect_reclaim_progress(void)
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
}

void test_page_cache_truncate_extend_zero_fill(void)
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
		TEST_ASSERT_EQ(vfs_write(file, (const char *)initial,
					 sizeof(initial)),
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
}

void test_page_cache_large_offset_rejected(void)
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
}

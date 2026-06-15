/*
 * test/buffer_test.c - Buffer Cache 自测
 *
 * 覆盖 bread 命中/未命中、引用计数、brelse，以及 bwrite 写穿透。
 * 测试写入磁盘末尾附近的 1KiB 区域，避开未来根文件系统常用区域。
 */

#include <kernel/blkdev.h>
#include <kernel/buffer.h>
#include <kernel/errno.h>
#include <kernel/string.h>
#include <kernel/test.h>
#include <drivers/virtio_blk.h>

#include "ktest.h"

void test_buffer_cache_basic(void)
{
	struct block_device *bdev;
	struct buffer_head *bh;
	struct buffer_head *again;
	static uint8_t disk_buf[BLOCK_SIZE];
	uint64_t block;
	uint64_t sector;
	uint32_t i;
	int ret;

	TEST_BEGIN("buffer cache: bread/brelse/bwrite");
	{
		bdev = lookup_block_device(ROOT_DEV);
		TEST_ASSERT_NOT_NULL(bdev);
		TEST_ASSERT(bdev->bd_sectors > BLOCK_SECTORS + 4);

		block = (bdev->bd_sectors - BLOCK_SECTORS - 2) /
			BLOCK_SECTORS;
		sector = block * BLOCK_SECTORS;

		bh = bread(ROOT_DEV, block);
		TEST_ASSERT_NOT_NULL(bh);
		TEST_ASSERT_NOT_NULL(bh->b_data);
		TEST_ASSERT_EQ(bh->b_dev, ROOT_DEV);
		TEST_ASSERT_EQ(bh->b_blocknr, block);
		TEST_ASSERT_EQ(bh->b_refcnt, 1);

		for (i = 0; i < BLOCK_SIZE; i++)
			bh->b_data[i] = (uint8_t)(i * 5u + 0x51u);
		bh->b_dirty = true;

		ret = bwrite(bh);
		TEST_ASSERT_EQ(ret, 0);
		TEST_ASSERT_EQ(bh->b_dirty, false);

		memset(disk_buf, 0, sizeof(disk_buf));
		ret = bdev->bd_ops->read_sectors(bdev, disk_buf, sector,
						  BLOCK_SECTORS);
		TEST_ASSERT_EQ(ret, 0);
		TEST_ASSERT_EQ(memcmp(bh->b_data, disk_buf, BLOCK_SIZE), 0);

		again = bread(ROOT_DEV, block);
		TEST_ASSERT_EQ(again, bh);
		TEST_ASSERT_EQ(bh->b_refcnt, 2);

		brelse(again);
		TEST_ASSERT_EQ(bh->b_refcnt, 1);
		brelse(bh);
		TEST_ASSERT_EQ(bh->b_refcnt, 0);
		brelse(bh);
		TEST_ASSERT_EQ(bh->b_refcnt, 0);
	}
	TEST_END("buffer cache: bread/brelse/bwrite");
	return;
fail:
	TEST_FAIL("buffer cache: bread/brelse/bwrite", "see above");
}

void test_buffer_cache_errors(void)
{
	struct buffer_head *bh;
	int ret;

	TEST_BEGIN("buffer cache: error paths");
	{
		bh = bread(MKDEV(9, 0), 0);
		TEST_ASSERT_NULL(bh);

		ret = bwrite(NULL);
		TEST_ASSERT_EQ(ret, -EINVAL);
	}
	TEST_END("buffer cache: error paths");
	return;
fail:
	TEST_FAIL("buffer cache: error paths", "see above");
}

void test_buffer_cache_eviction(void)
{
	struct block_device *bdev;
	uint64_t blocks;
	uint64_t start;

	TEST_BEGIN("buffer cache: eviction");
	{
		bdev = lookup_block_device(ROOT_DEV);
		TEST_ASSERT_NOT_NULL(bdev);

		blocks = bdev->bd_sectors / BLOCK_SECTORS;
		TEST_ASSERT(blocks > 700);
		start = blocks - 700;

		for (uint32_t i = 0; i < 600; i++) {
			struct buffer_head *bh = bread(ROOT_DEV, start + i);

			TEST_ASSERT_NOT_NULL(bh);
			brelse(bh);
		}
	}
	TEST_END("buffer cache: eviction");
	return;
fail:
	TEST_FAIL("buffer cache: eviction", "see above");
}

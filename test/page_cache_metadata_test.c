/*
 * test/page_cache_metadata_test.c - Page cache metadata block self-tests
 *
 * Covers raw block-device mapping lookup, write-through sync, repeated
 * acquisition, error handling, and clean metadata-page eviction under pressure.
 */

#include <drivers/virtio_blk.h>
#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <kernel/page_cache.h>
#include <kernel/test.h>

#include "ktest.h"

void test_page_cache_metadata_basic(void)
{
	struct block_device *bdev;
	struct page_cache *page;
	struct page_cache *again;
	static uint8_t disk_buf[BLOCK_SIZE];
	uint64_t block;
	uint64_t sector;
	uint8_t *data;
	int ret;

	TEST_BEGIN("page cache metadata: get/sync/reget");
	{
		TEST_ASSERT_EQ(BLOCK_SIZE, 4096u);
		TEST_ASSERT_EQ(BLOCK_SECTORS, 8u);

		bdev = lookup_block_device(ROOT_DEV);
		TEST_ASSERT_NOT_NULL(bdev);
		TEST_ASSERT(bdev->bd_sectors > BLOCK_SECTORS + 4);

		block = (bdev->bd_sectors - BLOCK_SECTORS - 2) / BLOCK_SECTORS;
		sector = block * BLOCK_SECTORS;

		page = page_cache_get_block(ROOT_DEV, block);
		TEST_ASSERT_NOT_NULL(page);
		data = page_cache_data(page);
		TEST_ASSERT_NOT_NULL(data);

		for (uint32_t i = 0; i < BLOCK_SIZE; i++)
			data[i] = (uint8_t)(i * 5u + 0x51u);

		ret = page_cache_sync_block(page);
		TEST_ASSERT_EQ(ret, 0);

		memset(disk_buf, 0, sizeof(disk_buf));
		ret = bdev->bd_ops->read_sectors(bdev, disk_buf, sector,
						 BLOCK_SECTORS);
		TEST_ASSERT_EQ(ret, 0);
		TEST_ASSERT_EQ(memcmp(data, disk_buf, BLOCK_SIZE), 0);

		again = page_cache_get_block(ROOT_DEV, block);
		TEST_ASSERT_EQ(again, page);
		page_cache_put_page(again);
		page_cache_put_page(page);
		page_cache_put_page(page);
	}
	TEST_END("page cache metadata: get/sync/reget");
	return;
fail:
	TEST_FAIL("page cache metadata: get/sync/reget", "see above");
}

void test_page_cache_metadata_errors(void)
{
	struct page_cache *page;
	int ret;

	TEST_BEGIN("page cache metadata: error paths");
	{
		page = page_cache_get_block(MKDEV(9, 0), 0);
		TEST_ASSERT_NULL(page);

		ret = page_cache_sync_block(NULL);
		TEST_ASSERT_EQ(ret, -EINVAL);
	}
	TEST_END("page cache metadata: error paths");
	return;
fail:
	TEST_FAIL("page cache metadata: error paths", "see above");
}

void test_page_cache_block_zero_writeback(void)
{
	struct page_cache *page;

	TEST_BEGIN("page cache metadata: block zero writeback");
	{
		/*
		 * Physical block 0 is a valid block-device mapping index.
		 * This catches regressions where map_block() uses 0 as an
		 * implicit failure sentinel instead of returning errno.
		 */
		page = page_cache_get_block(ROOT_DEV, 0);
		TEST_ASSERT_NOT_NULL(page);

		page_cache_mark_dirty(page);
		page_cache_put_page(page);
		TEST_ASSERT_EQ(page_cache_sync_all(), 0);
	}
	TEST_END("page cache metadata: block zero writeback");
	return;
fail:
	TEST_FAIL("page cache metadata: block zero writeback", "see above");
}

void test_page_cache_metadata_eviction(void)
{
	struct block_device *bdev;
	uint64_t blocks;
	uint64_t start;

	TEST_BEGIN("page cache metadata: eviction");
	{
		bdev = lookup_block_device(ROOT_DEV);
		TEST_ASSERT_NOT_NULL(bdev);

		blocks = bdev->bd_sectors / BLOCK_SECTORS;
		TEST_ASSERT(blocks > 700);
		start = blocks - 700;

		for (uint32_t i = 0; i < 600; i++) {
			struct page_cache *page =
				page_cache_get_block(ROOT_DEV, start + i);

			TEST_ASSERT_NOT_NULL(page);
			page_cache_put_page(page);
		}
	}
	TEST_END("page cache metadata: eviction");
	return;
fail:
	TEST_FAIL("page cache metadata: eviction", "see above");
}

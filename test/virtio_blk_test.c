/*
 * test/virtio_blk_test.c - virtio-blk 写/读回测试
 */

#include <kernel/test.h>
#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <drivers/virtio_blk.h>

void test_virtio_blk(void)
{

	enum { NSEC = 2 };
	static uint8_t wbuf[NSEC * SECTOR_SIZE];
	static uint8_t rbuf[NSEC * SECTOR_SIZE];
	struct block_device *bdev;
	uint64_t sector;
	int ret;
	unsigned int i;

	TEST_BEGIN("virtio_blk write/readback (2 sectors)");
	{
		bdev = lookup_block_device(ROOT_DEV);
		TEST_ASSERT_NOT_NULL(bdev);
		TEST_ASSERT(bdev->bd_sectors > NSEC + 1);


		sector = bdev->bd_sectors - NSEC - 1;


		for (i = 0; i < sizeof(wbuf); i++)
			wbuf[i] = (uint8_t)(i * 7u + 0x33u);


		ret = bdev->bd_ops->write_sectors(bdev, wbuf, sector, NSEC);
		TEST_ASSERT_EQ(ret, 0);


		memset(rbuf, 0, sizeof(rbuf));
		ret = bdev->bd_ops->read_sectors(bdev, rbuf, sector, NSEC);
		TEST_ASSERT_EQ(ret, 0);


		TEST_ASSERT_EQ(memcmp(wbuf, rbuf, sizeof(wbuf)), 0);
	}
	TEST_END("virtio_blk write/readback (2 sectors)");
	return;
fail:
	TEST_FAIL("virtio_blk write/readback (2 sectors)", "see above");
}

void test_virtio_blk_errors(void)
{
	struct block_device *bdev;

	struct block_device bad = {.bd_dev = MKDEV(40, 0)};
	static uint8_t buf[SECTOR_SIZE];
	int ret;

	TEST_BEGIN("virtio_blk error paths");
	{

		TEST_ASSERT_NULL(lookup_block_device(MKDEV(9, 0)));


		ret = register_block_device(&bad);
		TEST_ASSERT_EQ(ret, -EINVAL);


		bdev = lookup_block_device(ROOT_DEV);
		TEST_ASSERT_NOT_NULL(bdev);

		ret = bdev->bd_ops->read_sectors(bdev, buf, 0, 0);
		TEST_ASSERT_EQ(ret, -EINVAL);

		ret = bdev->bd_ops->read_sectors(bdev, buf, bdev->bd_sectors,
						 1);
		TEST_ASSERT_EQ(ret, -EINVAL);
	}
	TEST_END("virtio_blk error paths");
	return;
fail:
	TEST_FAIL("virtio_blk error paths", "see above");
}

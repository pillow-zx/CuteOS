/*
 * test/virtio_blk_test.c - virtio-blk 写/读回测试
 *
 * 功能：
 *   验证 virtio-blk 驱动的写路径与多扇区读写。在 DEBUG_ENABLE 下由
 *   kernel_test() 调用。向靠近磁盘末尾的扇区写入可识别模式，再读回比对，
 *   覆盖：write_sectors、read_sectors、nsec>1（2 扇区）及数据持久化。
 *
 *   读路径的 always-on smoke test 见 block/virtio_blk.c:vblk_smoke_test()。
 *
 * 注意：本测试会写入磁盘，故受 DEBUG_ENABLE 控制；写入位置选在磁盘末尾，
 *   避免破坏未来挂载的文件系统数据。
 */

#include <kernel/test.h>
#include <kernel/blkdev.h>
#include <kernel/string.h>

void test_virtio_blk(void)
{
	/* 写 2 个扇区，顺带验证 nsec>1 的多扇区路径 */
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

		/* 靠近磁盘末尾，留 1 扇区余量 */
		sector = bdev->bd_sectors - NSEC - 1;

		/* 填充可识别模式 */
		for (i = 0; i < sizeof(wbuf); i++)
			wbuf[i] = (uint8_t)(i * 7u + 0x33u);

		/* 写入 */
		ret = bdev->bd_ops->write_sectors(bdev, wbuf, sector, NSEC);
		TEST_ASSERT_EQ(ret, 0);

		/* 清空读缓冲后读回，确保数据确实来自设备 */
		memset(rbuf, 0, sizeof(rbuf));
		ret = bdev->bd_ops->read_sectors(bdev, rbuf, sector, NSEC);
		TEST_ASSERT_EQ(ret, 0);

		/* 比对：写入与读回必须完全一致 */
		TEST_ASSERT_EQ(memcmp(wbuf, rbuf, sizeof(wbuf)), 0);
	}
	TEST_END("virtio_blk write/readback (2 sectors)");
	return;
fail:
	TEST_FAIL("virtio_blk write/readback (2 sectors)", "see above");
}

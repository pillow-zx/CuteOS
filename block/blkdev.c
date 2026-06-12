/*
 * block/blkdev.c - 块设备注册与查找
 *
 * 功能：
 *   管理块设备的注册与查找。使用 dev_table[NR_BLOCK_DEVICES] 静态数组，
 *   以主设备号（MAJOR(dev)）为索引存储已注册的 struct block_device 指针。
 *   块设备驱动在初始化时调用 register_block_device() 注册自身；
 *   文件系统 / buffer cache 通过 lookup_block_device(dev) 按设备号取得
 *   操作向量，进而发起设备无关的扇区读写。
 *
 * 数据结构：
 *   dev_table[32] - 静态数组，索引为主设备号，元素为 block_device 指针
 *
 * 主要函数：
 *   register_block_device(bdev) - 注册块设备，以主设备号为索引
 *   lookup_block_device(dev)    - 按设备号查找已注册的块设备
 */

#include <kernel/blkdev.h>
#include <kernel/errno.h>

/* 支持的主设备号上限（virtio-blk 使用主设备号 8，足够） */
#define NR_BLOCK_DEVICES 32

static struct block_device *dev_table[NR_BLOCK_DEVICES];

/*
 * register_block_device - 注册块设备到 dev_table
 * @bdev: 待注册的块设备（bd_dev / bd_ops / bd_private 已就绪）
 *
 * 以 MAJOR(bdev->bd_dev) 为索引写入 dev_table。主设备号非法（≥ 32）
 * 返回 -EINVAL。同一主设备号重复注册会覆盖旧项（驱动假定唯一）。
 *
 * 返回值：0 成功，-EINVAL 主设备号越界。
 */
int register_block_device(struct block_device *bdev)
{
	uint32_t major = MAJOR(bdev->bd_dev);

	if (major >= NR_BLOCK_DEVICES)
		return -EINVAL;

	dev_table[major] = bdev;
	return 0;
}

/*
 * lookup_block_device - 按设备号查找块设备
 * @dev: 设备号
 *
 * 返回值：已注册的 struct block_device 指针；未注册返回 NULL。
 */
struct block_device *lookup_block_device(dev_t dev)
{
	uint32_t major = MAJOR(dev);

	if (major >= NR_BLOCK_DEVICES)
		return NULL;

	return dev_table[major];
}

#ifndef _CUTEOS_KERNEL_BLKDEV_H
#define _CUTEOS_KERNEL_BLKDEV_H

/*
 * include/kernel/blkdev.h - 块设备抽象层
 *
 * 在文件系统/buffer cache 与具体块设备驱动之间提供设备无关的扇区读写接口。
 * 调用方通过设备号查找到 struct block_device，再经 bd_ops->read_sectors /
 * write_sectors 发起 I/O，无需感知底层是 virtio-blk 还是其它驱动。
 *
 * 设备号编码（与 Linux kdev_t 一致）：高 12 位为主设备号，低 20 位为次设备号。
 *
 * Structs:
 *   struct block_device_operations - 扇区化操作向量（read/write_sectors）
 *   struct block_device            - 已注册块设备（设备号 + ops + 驱动私有数据）
 *
 * Functions:
 *   register_block_device(bdev) - 以主设备号为索引注册到 dev_table[]
 *   lookup_block_device(dev)    - 按设备号查找已注册的 struct block_device
 *
 * Globals:
 *   ROOT_DEV - 根文件系统所在块设备号：MKDEV(8, 0)（virtio-blk 主设备号 8）
 */

#include <kernel/types.h>

/* 设备号编码：高 12 位主设备号，低 20 位次设备号（与 Linux 一致）。
 * 用宏实现，使 MKDEV(maj,min) 成为编译期常量，可用于静态初始化。 */
#define MINORBITS	20u
#define MINORMASK	((1u << MINORBITS) - 1u)

#define MKDEV(major, minor) \
	((dev_t)(((dev_t)(major) << MINORBITS) | (dev_t)(minor)))
#define MAJOR(dev)	((unsigned int)((dev) >> MINORBITS))
#define MINOR(dev)	((unsigned int)((dev) & MINORMASK))

/* 块设备扇区大小：virtio-blk 与绝大多数块设备固定 512 字节/扇区 */
#define SECTOR_SIZE	512u
#define SECTOR_SHIFT	9

struct block_device;

/*
 * struct block_device_operations - 块设备扇区化操作向量
 *
 * @read_sectors:  从 @sector 起连续读 @nsec 个 512 字节扇区到 @buf
 * @write_sectors: 将 @buf 中 @nsec 个扇区写入 @sector 起
 *
 * 返回值：0 成功；负数错误码（-EIO / -EINVAL）失败。
 * @buf 必须位于内核直接映射区（驱动用 __pa() 取物理地址交给设备）。
 */
struct block_device_operations {
	int (*read_sectors)(struct block_device *bdev, void *buf,
			    uint64_t sector, uint32_t nsec);
	int (*write_sectors)(struct block_device *bdev, const void *buf,
			     uint64_t sector, uint32_t nsec);
};

/*
 * struct block_device - 已注册的块设备
 *
 * @bd_dev:     设备号（MKDEV(主, 次)）
 * @bd_sectors: 设备容量（512 字节扇区数），由驱动在注册前设置
 * @bd_ops:     操作向量
 * @bd_private: 驱动私有数据（如 virtio 驱动的 MMIO 基址等）
 */
struct block_device {
	dev_t bd_dev;
	uint64_t bd_sectors;
	const struct block_device_operations *bd_ops;
	void *bd_private;
};

/* 根文件系统块设备：virtio-blk，主设备号 8，次设备号 0 */
#define ROOT_DEV	MKDEV(8, 0)

/* 以主设备号为索引注册块设备；返回 0 成功，-EINVAL 主设备号非法或越界 */
int register_block_device(struct block_device *bdev);

/* 按设备号查找；未注册返回 NULL */
struct block_device *lookup_block_device(dev_t dev);

#endif /* _CUTEOS_KERNEL_BLKDEV_H */

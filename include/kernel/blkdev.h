#ifndef _CUTEOS_KERNEL_BLKDEV_H
#define _CUTEOS_KERNEL_BLKDEV_H

/*
 * include/kernel/blkdev.h - 块设备抽象层
 *
 * 声明块设备操作接口与全局设备表。文件系统和 buffer cache 通过
 * 这些操作函数发起读写请求。
 *
 * Structs:
 *   struct block_device_operations - Operation vector for a block device
 *     (open, release, read_block, write_block)
 *
 * Functions:
 *   register_device(dev, ops) - Register a block device in dev_table[]
 *
 * Globals:
 *   dev_table[] - Indexed by device number
 *   ROOT_DEV    - Device number of the root filesystem (MKDEV(8, 0))
 */

#endif

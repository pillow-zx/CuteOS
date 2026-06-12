#ifndef _CUTEOS_DRIVERS_VIRTIO_BLK_H
#define _CUTEOS_DRIVERS_VIRTIO_BLK_H

#include <kernel/blkdev.h>

/*
 * include/drivers/virtio_blk.h - virtio-blk 驱动对外接口
 *
 * 声明 virtio-blk MMIO 驱动的初始化入口，供 kernel_main 在各子系统
 * 初始化完成后调用。驱动内部以扇区（512 字节）为单位提供读写能力，
 * 并通过块设备抽象层（见 <kernel/blkdev.h>）注册到 dev_table，供
 * buffer cache / 文件系统以设备无关方式访问。
 *
 * Functions:
 *   virtio_blk_init() - 探测并初始化 virtio-blk 设备，注册块设备
 */

/*
 * virtio_blk_init - 初始化 virtio-blk MMIO 设备（modern 传输层，轮询模式）
 *
 * 依次完成：魔数/版本校验、设备复位、状态机推进（ACKNOWLEDGE → DRIVER）、
 * 协商 VIRTIO_F_VERSION_1（FEATURES_OK → DRIVER_OK）、建立 request virtqueue、
 * 读取磁盘容量，最后以主设备号 8 注册块设备。
 *
 * 失败（设备不存在 / 版本不符 / 队列过小 / 特性协商失败）调用 panic。
 * 成功后执行一次只读 smoke test（读扇区 0 + hexdump）以验证读路径。
 */
/* virtio-blk 主设备号：驱动注册于此（沿用传统块设备主设备号 8）。
 * 作为该设备号的唯一来源——ROOT_DEV 与驱动自身的 bd_dev 均由此派生，
 * 避免主设备号散落在抽象层与驱动两处。 */
#define VIRTIO_BLK_MAJOR	8u

/* 根文件系统所在块设备：virtio-blk（主设备号 8，次设备号 0）。
 * 根设备的选择属于平台策略，故置于驱动头而非通用块设备抽象层。 */
#define ROOT_DEV	MKDEV(VIRTIO_BLK_MAJOR, 0)

void virtio_blk_init(void);

#endif /* _CUTEOS_DRIVERS_VIRTIO_BLK_H */

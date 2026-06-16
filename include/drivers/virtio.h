/*
 * include/drivers/virtio.h - virtio 协议常量与数据结构
 *
 * 功能：
 *   集中定义 virtio MMIO 传输层、virtqueue（split ring）以及 virtio-blk
 *   设备所需的全部常量与数据结构。常量取值严格遵循 virtio 1.x 规范与
 *   Linux uapi 头（include/uapi/linux/virtio_{mmio,ring,blk}.h）。
 *
 * 本头文件不包含任何逻辑，仅提供：
 *     - MMIO 寄存器偏移与魔数（VIRTIO_MMIO_*）
 *     - 设备状态位（VIRTIO_CONFIG_S_*）
 *     - 特性位（VIRTIO_F_VERSION_1）
 *     - virtqueue 描述符/元素结构与标志位（struct vring_desc 等）
 *     - virtio-blk 请求头与状态码（virtio_blk_outhdr、VIRTIO_BLK_T_* 与 S_*）
 *     - 内存屏障原语（virtio_mb/wmb/rmb）
 *
 * 注意：
 *   代码库运行在小端 RISC-V（rv64），__virtio* / __le* 规范字段直接映射为
 *   原生 uint*_t，无需字节序转换。
 */

#ifndef _CUTEOS_DRIVERS_VIRTIO_H
#define _CUTEOS_DRIVERS_VIRTIO_H

#include <kernel/types.h>
#include <kernel/tools.h>

/* ---- QEMU virt 平台 virtio-mmio 总线 0 的 MMIO 基址 ----
 * QEMU virt 机器将 virtio-mmio 设备从 0x10001000 开始、按 0x1000 间隔排列。
 * Makefile 中 -device virtio-blk-device,bus=virtio-mmio-bus.0 挂在总线 0。
 */
#define VIRTIO_MMIO_BASE 0x10001000UL

#define VIRTIO_MMIO_MAGIC 0x74726976u

#define VIRTIO_MMIO_MAGIC_VALUE		0x000
#define VIRTIO_MMIO_VERSION		0x004
#define VIRTIO_MMIO_DEVICE_ID		0x008
#define VIRTIO_MMIO_VENDOR_ID		0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES	0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES	0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL		0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034
#define VIRTIO_MMIO_QUEUE_NUM		0x038
#define VIRTIO_MMIO_QUEUE_READY		0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064
#define VIRTIO_MMIO_STATUS		0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW	0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH	0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW	0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH	0x0a4
#define VIRTIO_MMIO_CONFIG_GENERATION	0x0fc
#define VIRTIO_MMIO_CONFIG		0x100

#define VIRTIO_CONFIG_S_ACKNOWLEDGE 0x01
#define VIRTIO_CONFIG_S_DRIVER	    0x02
#define VIRTIO_CONFIG_S_DRIVER_OK   0x04
#define VIRTIO_CONFIG_S_FEATURES_OK 0x08
#define VIRTIO_CONFIG_S_NEEDS_RESET 0x40
#define VIRTIO_CONFIG_S_FAILED	    0x80

#define VIRTIO_F_VERSION_1 32u

#define VRING_DESC_F_NEXT     0x01
#define VRING_DESC_F_WRITE    0x02
#define VRING_DESC_F_INDIRECT 0x04

#define VRING_DESC_ALIGN_SIZE  16
#define VRING_AVAIL_ALIGN_SIZE 2
#define VRING_USED_ALIGN_SIZE  4

struct vring_desc {
	paddr_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
};

struct vring_used_elem {
	uint32_t id;
	uint32_t len;
};

struct virtio_blk_outhdr {
	uint32_t type;
	uint32_t ioprio;
	uint64_t sector;
};

#define VIRTIO_BLK_T_IN	 0
#define VIRTIO_BLK_T_OUT 1

#define VIRTIO_BLK_S_OK	    0
#define VIRTIO_BLK_S_IOERR  1
#define VIRTIO_BLK_S_UNSUPP 2

/* ---- MMIO 寄存器读写辅助（32 位访问） ---- */

static inline void virtio_mmio_write(paddr_t base, uint32_t off, uint32_t val)
{
	MMIO_WRITE(uint32_t, base + off, val);
}

static inline uint32_t virtio_mmio_read(paddr_t base, uint32_t off)
{
	return MMIO_READ(uint32_t, base + off);
}

static inline void virtio_mmio_write64(paddr_t base, uint32_t low_off,
				       uint64_t val)
{
	virtio_mmio_write(base, low_off, (uint32_t)val);
	virtio_mmio_write(base, low_off + 4, (uint32_t)(val >> 32));
}

static inline void virtio_mb(void)
{
	asm volatile("fence rw,rw" ::: "memory");
}
static inline void virtio_wmb(void)
{
	asm volatile("fence w,w" ::: "memory");
}
static inline void virtio_rmb(void)
{
	asm volatile("fence r,r" ::: "memory");
}

#endif

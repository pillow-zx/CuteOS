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
#define VIRTIO_MMIO_BASE	0x10001000UL

/* virtio-mmio MagicValue 寄存器期望值：ASCII "virt" */
#define VIRTIO_MMIO_MAGIC	0x74726976u

/* ---- virtio-mmio 寄存器偏移（modern 传输层） ---- */
#define VIRTIO_MMIO_MAGIC_VALUE		0x000 /* R  魔数 "virt"          */
#define VIRTIO_MMIO_VERSION		0x004 /* R  版本（modern = 2）   */
#define VIRTIO_MMIO_DEVICE_ID		0x008 /* R  设备 ID（block = 2） */
#define VIRTIO_MMIO_VENDOR_ID		0x00c /* R  厂商 ID              */
#define VIRTIO_MMIO_DEVICE_FEATURES	0x010 /* R  设备支持特性（每集32位）*/
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL	0x014 /* W  设备特性集选择器      */
#define VIRTIO_MMIO_DRIVER_FEATURES	0x020 /* W  驱动激活的特性        */
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL	0x024 /* W  驱动特性集选择器      */
#define VIRTIO_MMIO_QUEUE_SEL		0x030 /* W  virtqueue 选择器      */
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034 /* R  当前队列最大长度      */
#define VIRTIO_MMIO_QUEUE_NUM		0x038 /* W  当前队列长度          */
#define VIRTIO_MMIO_QUEUE_READY		0x044 /* RW 队列就绪位（激活队列）*/
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050 /* W  通知设备处理某队列    */
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060 /* R  中断状态              */
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064 /* W  中断确认              */
#define VIRTIO_MMIO_STATUS		0x070 /* RW 设备状态              */
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080 /* W  描述符表地址低32位    */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084 /* W  描述符表地址高32位    */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW	0x090 /* W  可用环地址低32位      */
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH	0x094 /* W  可用环地址高32位      */
#define VIRTIO_MMIO_QUEUE_USED_LOW	0x0a0 /* W  已用环地址低32位      */
#define VIRTIO_MMIO_QUEUE_USED_HIGH	0x0a4 /* W  已用环地址高32位      */
#define VIRTIO_MMIO_CONFIG_GENERATION	0x0fc /* R  配置空间版本号        */
#define VIRTIO_MMIO_CONFIG		0x100 /* RW 设备特定配置空间      */

/* ---- 设备状态位（写入 STATUS 寄存器） ---- */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE	0x01  /* 驱动已发现设备            */
#define VIRTIO_CONFIG_S_DRIVER		0x02  /* 驱动已接管设备            */
#define VIRTIO_CONFIG_S_DRIVER_OK	0x04  /* 驱动初始化完成，可正常工作*/
#define VIRTIO_CONFIG_S_FEATURES_OK	0x08  /* 特性协商完成              */
#define VIRTIO_CONFIG_S_NEEDS_RESET	0x40  /* 设备要求复位（设备置位）  */
#define VIRTIO_CONFIG_S_FAILED		0x80  /* 驱动出错                  */

/* ---- 特性位 ----
 * VIRTIO_F_VERSION_1 = bit 32：声明使用 virtio 1.x（modern）语义。
 * 特性按 32 位为一集：word0 = bit 0..31（DEVICE_FEATURES sel=0），
 * word1 = bit 32..63（sel=1）。故 VERSION_1 落在 sel=1 的 bit 0。
 */
#define VIRTIO_F_VERSION_1		32u

/* ---- virtqueue（split virtqueue）描述符标志位 ---- */
#define VRING_DESC_F_NEXT		0x01 /* 指向下一个描述符（链）      */
#define VRING_DESC_F_WRITE		0x02 /* 设备可写（device-writable） */
#define VRING_DESC_F_INDIRECT		0x04 /* 间接描述符表                */

/* 环元素对齐要求（规范规定） */
#define VRING_DESC_ALIGN_SIZE		16
#define VRING_AVAIL_ALIGN_SIZE		2
#define VRING_USED_ALIGN_SIZE		4

/* ---- virtqueue 描述符表项（16 字节） ---- */
struct vring_desc {
	uint64_t addr; /* 缓冲区 guest 物理地址 */
	uint32_t len;  /* 缓冲区长度（字节）     */
	uint16_t flags;/* VRING_DESC_F_*         */
	uint16_t next; /* 下一个描述符索引       */
};

/* ---- 已用环元素：设备回传已处理链的信息 ---- */
struct vring_used_elem {
	uint32_t id;  /* 描述符链起始索引 */
	uint32_t len; /* 设备写入的总字节数 */
};

/* ---- virtio-blk 请求头（16 字节，链首描述符指向它） ---- */
struct virtio_blk_outhdr {
	uint32_t type;   /* VIRTIO_BLK_T_IN / VIRTIO_BLK_T_OUT */
	uint32_t ioprio; /* I/O 优先级（本驱动忽略）           */
	uint64_t sector; /* 起始扇区号（512 字节单位）         */
};

/* virtio-blk 命令类型 */
#define VIRTIO_BLK_T_IN		0 /* 读：设备→驱动 */
#define VIRTIO_BLK_T_OUT	1 /* 写：驱动→设备 */

/* virtio-blk 请求状态字节（链尾描述符指向它，设备回写） */
#define VIRTIO_BLK_S_OK		0 /* 成功       */
#define VIRTIO_BLK_S_IOERR	1 /* I/O 错误   */
#define VIRTIO_BLK_S_UNSUPP	2 /* 不支持     */

/* ---- MMIO 寄存器读写辅助（32 位访问） ---- */

static inline void virtio_mmio_write(uintptr_t base, uint32_t off, uint32_t val)
{
	MMIO_WRITE(uint32_t, base + off, val);
}

static inline uint32_t virtio_mmio_read(uintptr_t base, uint32_t off)
{
	return MMIO_READ(uint32_t, base + off);
}

/* ---- 内存屏障 ----
 * virtio 要求：提交描述符后、更新 avail->idx 前插入写屏障，使设备先看到
 * 描述符写入；读取 used->idx 后、读取结果数据前插入读屏障，保证读到完整
 * 结果。RISC-V 使用 fence 指令实现，"memory" clobber 阻止编译器重排。
 */
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

#endif /* _CUTEOS_DRIVERS_VIRTIO_H */

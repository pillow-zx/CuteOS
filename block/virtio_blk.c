/*
 * block/virtio_blk.c - virtio-blk MMIO 驱动（modern 传输层，轮询模式）
 *
 * 功能：
 *   驱动 QEMU virt 平台的 virtio-blk 虚拟块设备（MMIO 传输，modern 版本，
 *   与 Makefile 的 -global virtio-mmio.force-legacy=false 对应）。采用轮询模式：
 *   提交描述符链 → kick 通知 → 自旋等待 used ring 响应，不依赖中断。
 *
 *   探测与初始化（virtio_blk_init）遵循 virtio 1.x 状态机：
 *     reset → ACKNOWLEDGE → DRIVER → 特性协商（仅 VIRTIO_F_VERSION_1）
 *     → FEATURES_OK 校验 → DRIVER_OK → 建立 request virtqueue。
 *
 * 数据通路（virtio_blk_rw）每次 I/O 使用 3 个描述符的链：
 *     [请求头(16B, out)] → [数据缓冲(可变, in/out)] → [状态字节(1B, in)]
 *   轮询模式下同一时刻只有一个 in-flight 请求，故三个 ring 与请求结构体
 *   均为静态分配，驱动全程零 kmalloc。
 *
 *   读路径在 virtio_blk_init 末尾以读扇区 0 的 smoke test 验证；写路径的
 *   write/readback 验证见 test/virtio_blk_test.c（受 DEBUG_ENABLE 控制）。
 *
 * 主要接口：
 *   virtio_blk_init()                      - 探测、初始化、注册块设备
 *   virtio_blk_read_sectors / write_sectors- 经块设备操作向量调用（扇区单位）
 */

#include <drivers/virtio_blk.h>
#include <drivers/virtio.h>
#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/tools.h>
#include <asm/page.h>

/* ---- 配置 ---- */

/* request virtqueue 长度（2 的幂）。每次 I/O 用 3 个描述符，8 绰绰有余 */
#define VBLK_QSIZE	8

/* 单次请求最多扇区数（防御性上限，buffer cache 仅需 2） */
#define VBLK_MAX_SECTORS	256u

/* ---- 驱动本地 virtqueue 存储 ----
 * 按规范对齐：描述符表 16 字节、可用环 2 字节、已用环 4 字节。
 * 容量按 VBLK_QSIZE 固定，单 in-flight 下描述符 0/1/2 反复复用。
 */

/* 可用环：{flags, idx, ring[QSIZE], used_event} */
struct vblk_avail {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[VBLK_QSIZE];
	uint16_t used_event;
};

/* 已用环：{flags, idx, used_elem[QSIZE], avail_event} */
struct vblk_used {
	uint16_t flags;
	uint16_t idx;
	struct vring_used_elem ring[VBLK_QSIZE];
	uint16_t avail_event;
};

/* 单次请求：请求头 + 设备回写的状态字节 */
struct virtio_blk_req {
	struct virtio_blk_outhdr hdr;
	uint8_t status;
};

/* 驱动私有状态（挂在 block_device->bd_private） */
struct virtio_blk_dev {
	uintptr_t mmio_base;
	uint64_t capacity; /* 512 字节扇区总数 */
};

static struct vring_desc vblk_desc[VBLK_QSIZE]
	__aligned(VRING_DESC_ALIGN_SIZE);
static struct vblk_avail vblk_avail __aligned(VRING_AVAIL_ALIGN_SIZE);
static struct vblk_used vblk_used __aligned(VRING_USED_ALIGN_SIZE);
static struct virtio_blk_req vblk_req;
static struct virtio_blk_dev vblk_dev;

/* ---- 设备状态机辅助 ---- */

static void vblk_status_set(uintptr_t base, uint32_t bits)
{
	virtio_mmio_write(base, VIRTIO_MMIO_STATUS, bits);
}

static uint32_t vblk_status_get(uintptr_t base)
{
	return virtio_mmio_read(base, VIRTIO_MMIO_STATUS);
}

/* ---- virtqueue 建立（modern：写入各 ring 的 64 位物理地址后置 QueueReady） ---- */

static void vblk_setup_queue(uintptr_t base)
{
	uint32_t qnum_max;

	/* 选择 request queue（virtio-blk 仅使用队列 0） */
	virtio_mmio_write(base, VIRTIO_MMIO_QUEUE_SEL, 0);

	qnum_max = virtio_mmio_read(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
	if (qnum_max < VBLK_QSIZE)
		panic("virtio-blk: queue too small (max=%u, need=%u)\n",
		      qnum_max, VBLK_QSIZE);

	/* 清零 ring，避免残留引发设备误判 */
	memset(&vblk_desc, 0, sizeof(vblk_desc));
	memset(&vblk_avail, 0, sizeof(vblk_avail));
	memset(&vblk_used, 0, sizeof(vblk_used));

	virtio_mmio_write(base, VIRTIO_MMIO_QUEUE_NUM, VBLK_QSIZE);

	/* 描述符表 */
	virtio_mmio_write(base, VIRTIO_MMIO_QUEUE_DESC_LOW,
			  (uint32_t)__pa(vblk_desc));
	virtio_mmio_write(base, VIRTIO_MMIO_QUEUE_DESC_HIGH,
			  (uint32_t)(__pa(vblk_desc) >> 32));
	/* 可用环 */
	virtio_mmio_write(base, VIRTIO_MMIO_QUEUE_AVAIL_LOW,
			  (uint32_t)__pa(&vblk_avail));
	virtio_mmio_write(base, VIRTIO_MMIO_QUEUE_AVAIL_HIGH,
			  (uint32_t)(__pa(&vblk_avail) >> 32));
	/* 已用环 */
	virtio_mmio_write(base, VIRTIO_MMIO_QUEUE_USED_LOW,
			  (uint32_t)__pa(&vblk_used));
	virtio_mmio_write(base, VIRTIO_MMIO_QUEUE_USED_HIGH,
			  (uint32_t)(__pa(&vblk_used) >> 32));

	/* 激活队列 */
	virtio_mmio_write(base, VIRTIO_MMIO_QUEUE_READY, 1);
}

/* ---- 特性协商：仅接受 VIRTIO_F_VERSION_1（modern 必需） ---- */

static void vblk_negotiate_features(uintptr_t base)
{
	uint32_t status;

	/* 设备特性分两个 32 位集：sel 0 = bit 0..31，sel 1 = bit 32..63 */

	/* 驱动激活特性：sel 0 不接受任何 legacy 特性 */
	virtio_mmio_write(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
	virtio_mmio_write(base, VIRTIO_MMIO_DRIVER_FEATURES, 0);
	/* sel 1 接受 VIRTIO_F_VERSION_1（bit 32 → 集内 bit 0） */
	virtio_mmio_write(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
	virtio_mmio_write(base, VIRTIO_MMIO_DRIVER_FEATURES,
			  1u << (VIRTIO_F_VERSION_1 - 32));

	/* 声明特性集已敲定，并回读确认设备接受 */
	status = VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER |
		 VIRTIO_CONFIG_S_FEATURES_OK;
	vblk_status_set(base, status);

	if (!(vblk_status_get(base) & VIRTIO_CONFIG_S_FEATURES_OK))
		panic("virtio-blk: feature negotiation failed (VERSION_1 rejected)\n");
}

/* ---- 核心：提交一次读/写并轮询完成 ----
 *
 * @write: true=写（驱动→设备），false=读（设备→驱动）
 * @buf:   数据缓冲，必须位于内核直接映射区（驱动以 __pa() 取物理地址）
 *
 * 返回 0 成功；-EINVAL 参数越界；-EIO 设备报告错误。
 */
static int virtio_blk_rw(struct block_device *bdev, bool write, void *buf,
			 uint64_t sector, uint32_t nsec)
{
	struct virtio_blk_dev *vd = bdev->bd_private;
	uintptr_t base = vd->mmio_base;
	volatile uint16_t *used_idx = (volatile uint16_t *)&vblk_used.idx;
	uint16_t expected;

	/* 参数校验（溢出安全） */
	if (nsec == 0 || nsec > VBLK_MAX_SECTORS)
		return -EINVAL;
	if (nsec > vd->capacity || sector > vd->capacity - nsec)
		return -EINVAL;

	/* 组织请求 */
	vblk_req.hdr.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
	vblk_req.hdr.ioprio = 0;
	vblk_req.hdr.sector = sector;
	vblk_req.status = 0xff; /* 哨兵：完成后设备写 0(S_OK) */

	/* desc 0：请求头，设备可读，指向下一个 */
	vblk_desc[0].addr = __pa(&vblk_req.hdr);
	vblk_desc[0].len = sizeof(vblk_req.hdr);
	vblk_desc[0].flags = VRING_DESC_F_NEXT;
	vblk_desc[0].next = 1;

	/* desc 1：数据缓冲。写→设备可读，读→设备可写 */
	vblk_desc[1].addr = __pa(buf);
	vblk_desc[1].len = (uint32_t)nsec * SECTOR_SIZE;
	vblk_desc[1].flags = VRING_DESC_F_NEXT |
			     (write ? 0 : VRING_DESC_F_WRITE);
	vblk_desc[1].next = 2;

	/* desc 2：状态字节，设备可写，链尾 */
	vblk_desc[2].addr = __pa(&vblk_req.status);
	vblk_desc[2].len = sizeof(vblk_req.status);
	vblk_desc[2].flags = VRING_DESC_F_WRITE;
	vblk_desc[2].next = 0;

	/* 发布到可用环（head = 描述符 0） */
	expected = (uint16_t)(vblk_avail.idx + 1);
	vblk_avail.ring[vblk_avail.idx % VBLK_QSIZE] = 0;
	virtio_wmb(); /* 先让设备看到描述符写入，再更新 idx */
	vblk_avail.idx = expected;

	/* kick：通知设备处理队列 0 */
	virtio_mmio_write(base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

	/* 轮询等待设备消费本次请求（used->idx 追上 expected） */
	while (*used_idx != expected)
		;
	virtio_rmb(); /* 先确认 idx 推进，再读取结果数据 */

	/* 校验设备状态字节 */
	if (vblk_req.status != VIRTIO_BLK_S_OK)
		return -EIO;

	return 0;
}

/* ---- 块设备操作向量 ---- */

static int virtio_blk_read_sectors(struct block_device *bdev, void *buf,
				   uint64_t sector, uint32_t nsec)
{
	return virtio_blk_rw(bdev, false, buf, sector, nsec);
}

static int virtio_blk_write_sectors(struct block_device *bdev, const void *buf,
				    uint64_t sector, uint32_t nsec)
{
	return virtio_blk_rw(bdev, true, (void *)buf, sector, nsec);
}

static const struct block_device_operations vblk_ops = {
	.read_sectors = virtio_blk_read_sectors,
	.write_sectors = virtio_blk_write_sectors,
};

/* 静态块设备实例（主设备号 8） */
static struct block_device vblk_bdev = {
	.bd_dev = MKDEV(8, 0),
	.bd_ops = &vblk_ops,
	.bd_private = &vblk_dev,
};

/* ---- 只读 smoke test：读扇区 0 并 hexdump 前 32 字节 ---- */

static void vblk_smoke_test(void)
{
	static uint8_t sect0[SECTOR_SIZE];
	int ret;

	ret = virtio_blk_rw(&vblk_bdev, false, sect0, 0, 1);
	if (ret) {
		printk("virtio_blk: sector 0 read failed (%d)\n", ret);
		return;
	}
	printk("virtio_blk: sector 0 first 32 bytes:\n");
	print_hexdump(sect0, 32);
}

/* ---- 初始化入口 ---- */

void virtio_blk_init(void)
{
	uintptr_t base = VIRTIO_MMIO_BASE;
	uint32_t magic, version, cap_lo, cap_hi;
	uint32_t status;

	/* 探测：魔数 + 版本（modern = 2） */
	magic = virtio_mmio_read(base, VIRTIO_MMIO_MAGIC_VALUE);
	if (magic != VIRTIO_MMIO_MAGIC)
		panic("virtio-blk: bad magic 0x%x at 0x%lx (expected 0x%x)\n",
		      magic, base, VIRTIO_MMIO_MAGIC);

	version = virtio_mmio_read(base, VIRTIO_MMIO_VERSION);
	if (version != 2)
		panic("virtio-blk: unsupported transport version %u (need modern=2)\n",
		      version);

	/* 复位 */
	vblk_status_set(base, 0);

	/* ACKNOWLEDGE：驱动已发现设备 */
	vblk_status_set(base, VIRTIO_CONFIG_S_ACKNOWLEDGE);

	/* DRIVER：驱动正接管设备 */
	vblk_status_set(base,
			VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);

	/* 特性协商 */
	vblk_negotiate_features(base);

	/* DRIVER_OK：驱动初始化完成，设备可正常处理请求 */
	status = VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER |
		 VIRTIO_CONFIG_S_FEATURES_OK | VIRTIO_CONFIG_S_DRIVER_OK;
	vblk_status_set(base, status);

	/* 建立 request virtqueue */
	vblk_setup_queue(base);

	/* 读取磁盘容量（config 空间前 8 字节，512 字节扇区数） */
	cap_lo = virtio_mmio_read(base, VIRTIO_MMIO_CONFIG);
	cap_hi = virtio_mmio_read(base, VIRTIO_MMIO_CONFIG + 4);
	vblk_dev.mmio_base = base;
	vblk_dev.capacity = (uint64_t)cap_lo | ((uint64_t)cap_hi << 32);

	/* 暴露容量给块设备抽象层（buffer cache / EXT2 需要） */
	vblk_bdev.bd_sectors = vblk_dev.capacity;

	/* 注册块设备 */
	register_block_device(&vblk_bdev);

	printk("virtio_blk: init ok, capacity=%llu sectors (%llu MB)\n",
	       (unsigned long long)vblk_dev.capacity,
	       (unsigned long long)(vblk_dev.capacity >> 11));

	/* 读路径 smoke test */
	vblk_smoke_test();
}

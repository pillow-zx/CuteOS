/*
 * block/blkdev.c - 块设备注册与查找
 *
 * 功能：
 *   管理块设备的注册与查找。使用 dev_table[NR_BLOCK_DEVICES] 静态数组，
 *   以主设备号（MAJOR(dev)）为索引存储已注册的 struct block_device 指针。
 *   块设备驱动在初始化时调用 register_block_device() 注册自身；
 *   文件系统 / page cache 通过 lookup_block_device(dev) 按设备号取得
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
 * 块设备 mapping 是 page cache 的“物理块命名域”。index 直接表示 4 KiB
 * 物理块号，因此 readpage/writepages 只需要把块号换算成 512 字节扇区号。
 * 这让 ext2 元数据块仍可按物理块缓存，同时复用 inode 文件页的通用回写路径。
 */
static int block_mapping_readpage(struct page_mapping *mapping, uint64_t index,
				  void *data)
{
	struct block_device *bdev = mapping ? mapping->host : NULL;

	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->read_sectors || !data)
		return -ENXIO;

	return bdev->bd_ops->read_sectors(bdev, data, index * BLOCK_SECTORS,
					  BLOCK_SECTORS);
}

static int block_mapping_map_block(struct page_mapping *mapping,
				   uint64_t index, bool create,
				   uint32_t *block)
{
	(void)mapping;
	(void)create;

	if (!block)
		return -EINVAL;
	if (index > UINT32_MAX)
		return -EFBIG;

	/* 物理块 mapping 不需要分配或翻译，逻辑块号就是物理块号。 */
	*block = (uint32_t)index;
	return 0;
}

static int block_mapping_writepages(struct page_mapping *mapping,
				    uint64_t start_index, uint32_t nr_pages,
				    const void *data)
{
	struct block_device *bdev = mapping ? mapping->host : NULL;

	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->write_sectors || !data)
		return -ENXIO;

	return bdev->bd_ops->write_sectors(bdev, data,
					   start_index * BLOCK_SECTORS,
					   nr_pages * BLOCK_SECTORS);
}

static const struct page_mapping_ops block_mapping_aops = {
	.readpage = block_mapping_readpage,
	.map_block = block_mapping_map_block,
	.writepages = block_mapping_writepages,
};

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

	page_mapping_init(&bdev->bd_pages, bdev, &block_mapping_aops, NULL);
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

struct page_mapping *block_device_pages(dev_t dev)
{
	struct block_device *bdev = lookup_block_device(dev);

	if (!bdev)
		return NULL;

	/*
	 * 正常路径在 register_block_device() 初始化 bd_pages。这里保留惰性
	 * 修复，是为了兼容静态测试对象或早期初始化中手工填充的 block_device。
	 */
	if (!bdev->bd_pages.pages.next || !bdev->bd_pages.ops)
		page_mapping_init(&bdev->bd_pages, bdev, &block_mapping_aops,
				  NULL);
	return &bdev->bd_pages;
}

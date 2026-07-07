/*
 * block/blkdev.c - 块设备注册与查找
 */

#include <kernel/blkdev.h>
#include <kernel/errno.h>

#define NR_BLOCK_DEVICES 32

static struct block_device *dev_table[NR_BLOCK_DEVICES];

static int block_mapping_readpage(struct page_mapping *mapping, uint64_t index,
				  void *data)
{
	struct block_device *bdev = mapping ? mapping->host : NULL;

	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->read_sectors || !data)
		return -ENXIO;

	return bdev->bd_ops->read_sectors(bdev, data, index * BLOCK_SECTORS,
					  BLOCK_SECTORS);
}

static int block_mapping_map_block(struct page_mapping *mapping, uint64_t index,
				   bool create, uint32_t *block)
{
	(void)mapping;
	(void)create;

	if (!block)
		return -EINVAL;
	if (index > UINT32_MAX)
		return -EFBIG;


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

int register_block_device(struct block_device *bdev)
{
	uint32_t major = MAJOR(bdev->bd_dev);

	if (major >= NR_BLOCK_DEVICES)
		return -EINVAL;

	page_mapping_init(&bdev->bd_pages, bdev, &block_mapping_aops, NULL);
	dev_table[major] = bdev;
	return 0;
}

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


	if (!bdev->bd_pages.pages.next || !bdev->bd_pages.ops)
		page_mapping_init(&bdev->bd_pages, bdev, &block_mapping_aops,
				  NULL);
	return &bdev->bd_pages;
}

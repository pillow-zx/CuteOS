/* block/blkdev.c - block-device registration and lookup */

#include <kernel/blkdev.h>
#include <kernel/errno.h>

#define NR_BLOCK_DEVICES 32

static struct block_device *dev_table[NR_BLOCK_DEVICES];

static int block_mapping_resolve(struct page_mapping *mapping, uint64_t index,
				 bool create, uint64_t *block)
{
	(void)create;
	if (!mapping || !block)
		return -EINVAL;
	if (index > UINT32_MAX)
		return -EFBIG;
	*block = index;
	return 0;
}

static const struct page_mapping_ops block_mapping_ops = {
	.resolve = block_mapping_resolve,
};

int register_block_device(struct block_device *bdev)
{
	uint32_t major;
	if (!bdev)
		return -EINVAL;
	major = MAJOR(bdev->bd_dev);
	if (major >= NR_BLOCK_DEVICES)
		return -EINVAL;
	page_mapping_init(&bdev->bd_pages, bdev, bdev->bd_dev,
			  &block_mapping_ops);
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
	if (!bdev->bd_pages.ops)
		page_mapping_init(&bdev->bd_pages, bdev, dev, &block_mapping_ops);
	return &bdev->bd_pages;
}

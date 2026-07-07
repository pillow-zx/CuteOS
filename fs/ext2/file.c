#include "ext2.h"

#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <kernel/page_cache.h>

static bool ext2_file_index_valid(uint64_t index)
{
	return index <= EXT2_MAX_FILE_INDEX;
}

static bool ext2_file_page_range_valid(uint64_t start, uint32_t nr_pages)
{
	if (nr_pages == 0 || !ext2_file_index_valid(start))
		return false;

	return nr_pages - 1 <= EXT2_MAX_FILE_INDEX - start;
}

static int ext2_readpage(struct page_mapping *mapping, uint64_t index,
			 void *data)
{
	struct inode *inode = mapping ? mapping->host : NULL;
	struct block_device *bdev;
	uint32_t pblock;

	if (!inode || !data)
		return -EINVAL;
	if (!ext2_file_index_valid(index))
		return -EFBIG;

	pblock = ext2_bmap_readonly(inode, (uint32_t)index);
	if (!pblock) {

		memset(data, 0, BLOCK_SIZE);
		return 0;
	}

	bdev = lookup_block_device(inode->i_sb->s_dev);
	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->read_sectors)
		return -ENXIO;

	return bdev->bd_ops->read_sectors(bdev, data, pblock * BLOCK_SECTORS,
					  BLOCK_SECTORS);
}

static int ext2_map_block(struct page_mapping *mapping, uint64_t index,
			  bool create, uint32_t *block)
{
	struct inode *inode = mapping ? mapping->host : NULL;
	uint32_t pblock;

	if (!inode || !ext2_file_index_valid(index))
		return !inode ? -EINVAL : -EFBIG;
	if (!block)
		return -EINVAL;

	if (create)
		pblock = ext2_bmap(inode, (uint32_t)index, true);
	else
		pblock = ext2_bmap_readonly(inode, (uint32_t)index);

	if (!pblock)
		return create ? -ENOSPC : -EIO;

	*block = pblock;
	return 0;
}

static int ext2_writepages(struct page_mapping *mapping, uint64_t start_index,
			   uint32_t nr_pages, const void *data)
{
	struct inode *inode = mapping ? mapping->host : NULL;
	struct block_device *bdev;
	uint32_t pblock;

	if (!inode || !data || nr_pages == 0)
		return -EINVAL;
	if (!ext2_file_page_range_valid(start_index, nr_pages))
		return -EFBIG;

	pblock = ext2_bmap_readonly(inode, (uint32_t)start_index);
	if (!pblock)
		return -EIO;

	bdev = lookup_block_device(inode->i_sb->s_dev);
	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->write_sectors)
		return -ENXIO;

	return bdev->bd_ops->write_sectors(bdev, data, pblock * BLOCK_SECTORS,
					   nr_pages * BLOCK_SECTORS);
}

const struct page_mapping_ops ext2_inode_aops = {
	.readpage = ext2_readpage,
	.map_block = ext2_map_block,
	.writepages = ext2_writepages,
};

ssize_t ext2_read_file(struct inode *inode, char *buf, size_t count, loff_t pos)
{
	size_t done = 0;
	uint64_t readable_size;

	if (!inode || !buf)
		return -EINVAL;
	if (pos < 0)
		return -EINVAL;

	readable_size = inode->i_size;
	if (readable_size > EXT2_MAX_FILE_SIZE)
		readable_size = EXT2_MAX_FILE_SIZE;

	if ((uint64_t)pos >= readable_size)
		return 0;
	if (count > readable_size - (uint64_t)pos)
		count = readable_size - (uint64_t)pos;

	while (done < count) {
		struct page_cache *page;
		uint64_t file_pos = (uint64_t)pos + done;
		uint32_t lblock = (uint32_t)(file_pos / BLOCK_SIZE);
		uint32_t offset = (uint32_t)(file_pos % BLOCK_SIZE);
		size_t chunk = BLOCK_SIZE - offset;

		if (chunk > count - done)
			chunk = count - done;

		page = page_cache_read_page(&inode->i_pages, lblock);
		if (!page)
			return done ? (ssize_t)done : -EIO;

		memcpy(buf + done, page_cache_data(page) + offset, chunk);
		page_cache_put_page(page);
		done += chunk;
	}

	return (ssize_t)done;
}

ssize_t ext2_write_file(struct inode *inode, const char *buf, size_t count,
			loff_t pos)
{
	size_t done = 0;
	uint64_t writable;

	if (!inode || !buf)
		return -EINVAL;
	if (pos < 0)
		return -EINVAL;
	if ((uint64_t)pos >= EXT2_MAX_FILE_SIZE)
		return count == 0 ? 0 : -EFBIG;

	writable = EXT2_MAX_FILE_SIZE - (uint64_t)pos;
	if ((uint64_t)count > writable)
		count = (size_t)writable;

	while (done < count) {
		struct page_cache *page;
		bool created = false;
		uint64_t file_pos = (uint64_t)pos + done;
		uint32_t lblock = (uint32_t)(file_pos / BLOCK_SIZE);
		uint32_t offset = (uint32_t)(file_pos % BLOCK_SIZE);
		size_t chunk = BLOCK_SIZE - offset;
		uint32_t pblock;
		int ret;

		if (chunk > count - done)
			chunk = count - done;

		page = page_cache_grab_file_page(inode, lblock, true, &created);
		if (!page)
			return done ? (ssize_t)done : -ENOMEM;

		ret = inode->i_pages.ops->map_block(&inode->i_pages, lblock,
						    false, &pblock);
		if (ret < 0) {
			if (ret != -EIO) {
				page_cache_put_page(page);
				return done ? (ssize_t)done : ret;
			}
			ret = inode->i_pages.ops->map_block(
				&inode->i_pages, lblock, true, &pblock);
			if (ret < 0) {
				page_cache_put_page(page);
				return done ? (ssize_t)done : ret;
			}

			if (!page_cache_is_uptodate(page)) {
				memset(page_cache_data(page), 0, BLOCK_SIZE);
				page_cache_set_uptodate(page, true);
			}
		} else if (!page_cache_is_uptodate(page) &&
			   !(offset == 0 && chunk == BLOCK_SIZE)) {

			ret = inode->i_pages.ops->readpage(
				&inode->i_pages, lblock, page_cache_data(page));
			if (ret < 0) {
				page_cache_put_page(page);
				return done ? (ssize_t)done : ret;
			}
			page_cache_set_uptodate(page, true);
		} else if (!page_cache_is_uptodate(page) && created &&
			   offset == 0 && chunk == BLOCK_SIZE) {

			page_cache_set_uptodate(page, true);
		} else if (!page_cache_is_uptodate(page) && offset == 0 &&
			   chunk == BLOCK_SIZE) {

			page_cache_set_uptodate(page, true);
		}

		memcpy(page_cache_data(page) + offset, buf + done, chunk);
		page_cache_mark_dirty(page);
		page_cache_put_page(page);
		done += chunk;
	}

	if ((uint64_t)pos + done > inode->i_size) {
		inode->i_size = (uint64_t)pos + done;
		ext2_write_inode(inode);
	}

	return (ssize_t)done;
}

static ssize_t ext2_file_read(struct file *file, char *buf, size_t count)
{
	return ext2_read_file(file->f_inode, buf, count, file->f_pos);
}

static ssize_t ext2_file_write(struct file *file, const char *buf, size_t count)
{
	return ext2_write_file(file->f_inode, buf, count, file->f_pos);
}

const struct file_operations ext2_file_operations = {
	.read = ext2_file_read,
	.write = ext2_file_write,
};

/* In-memory block and regular-file fixtures for kernel mechanism tests. */

#include "memory_fixture.h"

#include <kernel/errno.h>
#include <kernel/page_cache.h>
#include <kernel/slab.h>

static uint8_t memory_storage[KTEST_MEMORY_BLOCKS * BLOCK_SIZE];
static struct block_device memory_bdev;
static bool memory_device_registered;
static struct ktest_memory_stats memory_stats;
static int memory_fail_next_read;
static int memory_fail_next_write;

static int memory_read_sectors(struct block_device *bdev, void *buf,
			       uint64_t sector, uint32_t nsec)
{
	uint64_t byte_offset;
	uint64_t byte_count;

	(void)bdev;
	if (!buf || nsec == 0 || nsec > memory_bdev.bd_sectors ||
	    sector > memory_bdev.bd_sectors - nsec)
		return -EINVAL;
	if (memory_fail_next_read) {
		int error = memory_fail_next_read;

		memory_fail_next_read = 0;
		return error;
	}

	byte_offset = sector * SECTOR_SIZE;
	byte_count = (uint64_t)nsec * SECTOR_SIZE;
	memcpy(buf, memory_storage + byte_offset, byte_count);
	memory_stats.read_reqs++;
	return 0;
}

static int memory_write_sectors(struct block_device *bdev, const void *buf,
				uint64_t sector, uint32_t nsec)
{
	uint64_t byte_offset;
	uint64_t byte_count;

	(void)bdev;
	if (!buf || nsec == 0 || nsec > memory_bdev.bd_sectors ||
	    sector > memory_bdev.bd_sectors - nsec)
		return -EINVAL;
	if (memory_fail_next_write) {
		int error = memory_fail_next_write;

		memory_fail_next_write = 0;
		return error;
	}

	byte_offset = sector * SECTOR_SIZE;
	byte_count = (uint64_t)nsec * SECTOR_SIZE;
	memcpy(memory_storage + byte_offset, buf, byte_count);
	memory_stats.write_reqs++;
	memory_stats.last_write_nsec = nsec;
	if (nsec > memory_stats.max_write_nsec)
		memory_stats.max_write_nsec = nsec;
	return 0;
}

static const struct block_device_operations memory_bdev_ops = {
	.read_sectors = memory_read_sectors,
	.write_sectors = memory_write_sectors,
};

static int memory_file_resolve(struct page_mapping *mapping, uint64_t index,
			       bool create, uint64_t *block)
{
	struct ktest_memory_file *file = mapping ? mapping->host : NULL;

	if (!file || !block)
		return -EINVAL;
	if (index >= KTEST_MEMORY_FILE_BLOCKS)
		return -EFBIG;
	if (!create && !file->allocated[index])
		return -ENODATA;
	if (create)
		file->allocated[index] = true;
	*block = file->first_block + index;
	return 0;
}

static const struct page_mapping_ops memory_file_mapping_ops = {
	.resolve = memory_file_resolve,
};

static int memory_inode_writeback(struct inode *inode)
{
	(void)inode;
	return 0;
}

static int memory_file_truncate(struct inode *inode, uint64_t size)
{
	struct ktest_memory_file *file = inode ? inode->i_private : NULL;

	if (!file || size > (uint64_t)KTEST_MEMORY_FILE_BLOCKS * BLOCK_SIZE)
		return -EFBIG;
	page_cache_truncate_inode(inode, size);
	inode->i_size = size;
	return 0;
}

static const struct inode_operations memory_file_inode_ops = {
	.truncate = memory_file_truncate,
};

static ssize_t memory_file_read(struct file *open_file, char *buf,
				size_t count)
{
	struct ktest_memory_file *file = open_file ?
		open_file->f_inode->i_private : NULL;
	uint64_t pos;
	size_t done = 0;

	if (!file || !buf || open_file->f_pos < 0)
		return -EINVAL;
	pos = (uint64_t)open_file->f_pos;
	if (pos >= file->inode.i_size)
		return 0;
	if (count > file->inode.i_size - pos)
		count = file->inode.i_size - pos;

	while (done < count) {
		uint64_t file_pos = pos + done;
		uint32_t index = file_pos / BLOCK_SIZE;
		uint32_t offset = file_pos % BLOCK_SIZE;
		size_t chunk = BLOCK_SIZE - offset;
		struct page_cache *page;
		int error = 0;

		if (chunk > count - done)
			chunk = count - done;
		page = page_cache_get_mapping(&file->inode.i_pages, index,
					      PAGE_CACHE_READ, &error);
		if (!page && error == -ENODATA) {
			memset(buf + done, 0, chunk);
			done += chunk;
			continue;
		}
		if (!page)
			return done ? (ssize_t)done : error;
		memcpy(buf + done, page_cache_data(page) + offset, chunk);
		page_cache_put_page(page);
		done += chunk;
	}

	return done;
}

static ssize_t memory_file_write(struct file *open_file, const char *buf,
				 size_t count)
{
	struct ktest_memory_file *file = open_file ?
		open_file->f_inode->i_private : NULL;
	uint64_t pos;
	size_t done = 0;

	if (!file || !buf || open_file->f_pos < 0)
		return -EINVAL;
	pos = (uint64_t)open_file->f_pos;
	if (pos >= (uint64_t)KTEST_MEMORY_FILE_BLOCKS * BLOCK_SIZE)
		return count ? -EFBIG : 0;
	if (count > (uint64_t)KTEST_MEMORY_FILE_BLOCKS * BLOCK_SIZE - pos)
		count = (size_t)((uint64_t)KTEST_MEMORY_FILE_BLOCKS * BLOCK_SIZE -
				 pos);

	while (done < count) {
		uint64_t file_pos = pos + done;
		uint32_t index = file_pos / BLOCK_SIZE;
		uint32_t offset = file_pos % BLOCK_SIZE;
		size_t chunk = BLOCK_SIZE - offset;
		struct page_cache *page;
		uint32_t flags = PAGE_CACHE_READ | PAGE_CACHE_CREATE;

		if (chunk > count - done)
			chunk = count - done;
		if (offset == 0 && chunk == BLOCK_SIZE)
			flags = PAGE_CACHE_CREATE;
		page = page_cache_get_mapping(&file->inode.i_pages, index, flags,
					      NULL);
		if (!page)
			return done ? (ssize_t)done : -ENOMEM;
		memcpy(page_cache_data(page) + offset, buf + done, chunk);
		page_cache_mark_dirty(page);
		page_cache_put_page(page);
		done += chunk;
	}

	if (pos + done > file->inode.i_size)
		file->inode.i_size = pos + done;
	return done;
}

static const struct file_operations memory_file_ops = {
	.read = memory_file_read,
	.write = memory_file_write,
};

static const struct super_operations memory_super_ops = {
	.write_inode = memory_inode_writeback,
	.datasync_inode = memory_inode_writeback,
};

int ktest_memory_device_init(void)
{
	int ret;

	if (memory_device_registered)
		return 0;

	memset(&memory_bdev, 0, sizeof(memory_bdev));
	memory_bdev.bd_dev = KTEST_MEMORY_DEV(0);
	memory_bdev.bd_sectors =
		(uint64_t)KTEST_MEMORY_BLOCKS * BLOCK_SECTORS;
	memory_bdev.bd_ops = &memory_bdev_ops;
	ret = register_block_device(&memory_bdev);
	if (ret < 0)
		return ret;
	memory_device_registered = true;
	return 0;
}

void ktest_memory_device_clear(void)
{
	memset(memory_storage, 0, sizeof(memory_storage));
}

void ktest_memory_device_reset_stats(void)
{
	memset(&memory_stats, 0, sizeof(memory_stats));
}

void ktest_memory_device_get_stats(struct ktest_memory_stats *stats)
{
	if (stats)
		*stats = memory_stats;
}

void ktest_memory_device_fail_next_read(int error)
{
	memory_fail_next_read = error;
}

void ktest_memory_device_fail_next_write(int error)
{
	memory_fail_next_write = error;
}

int ktest_memory_file_init(struct ktest_memory_file *file, dev_t dev,
			   uint32_t first_block)
{
	if (!file || MAJOR(dev) != KTEST_MEMORY_MAJOR ||
	    first_block >= KTEST_MEMORY_BLOCKS ||
	    KTEST_MEMORY_FILE_BLOCKS > KTEST_MEMORY_BLOCKS - first_block)
		return -EINVAL;
	if (ktest_memory_device_init() < 0)
		return -ENODEV;

	memset(file, 0, sizeof(*file));
	file->dev = dev;
	file->first_block = first_block;
	memset(memory_storage + (size_t)first_block * BLOCK_SIZE, 0,
	       (size_t)KTEST_MEMORY_FILE_BLOCKS * BLOCK_SIZE);

	file->sb.s_dev = dev;
	file->sb.s_blocksize = BLOCK_SIZE;
	file->sb.s_op = &memory_super_ops;
	INIT_LIST_HEAD(&file->sb.s_inodes);

	file->inode.i_ino = 1;
	file->inode.i_mode = S_IFREG | 0600;
	file->inode.i_nlink = 1;
	file->inode.i_sb = &file->sb;
	file->inode.i_op = &memory_file_inode_ops;
	file->inode.i_fop = &memory_file_ops;
	file->inode.i_private = file;
	refcount_set(&file->inode.i_refcount, 1);
	INIT_LIST_HEAD(&file->inode.i_hash);
	INIT_LIST_HEAD(&file->inode.i_sb_list);
	page_mapping_init(&file->inode.i_pages, file, dev,
			  &memory_file_mapping_ops);

	file->file.f_op = &memory_file_ops;
	file->file.f_inode = &file->inode;
	file->file.f_flags = O_RDWR;
	file->file.f_mode = FMODE_READ | FMODE_WRITE;
	file->file.static_file = true;
	refcount_set(&file->file.refcount, 1);
	return 0;
}

void ktest_memory_file_destroy(struct ktest_memory_file *file)
{
	int ret;

	if (!file)
		return;
	ret = page_cache_sync_mapping(&file->inode.i_pages);
	(void)ret;
	page_cache_invalidate_mapping(&file->inode.i_pages);
}

static int memory_file_index_valid(const struct ktest_memory_file *file,
				   uint32_t index)
{
	return file && index < KTEST_MEMORY_FILE_BLOCKS ? 0 : -EINVAL;
}

int ktest_memory_file_seed_block(struct ktest_memory_file *file,
				uint32_t index, const void *data)
{
	if (memory_file_index_valid(file, index) < 0 || !data)
		return -EINVAL;
	file->allocated[index] = true;
	memcpy(memory_storage + (size_t)(file->first_block + index) * BLOCK_SIZE,
	       data, BLOCK_SIZE);
	return 0;
}

int ktest_memory_file_read_block(const struct ktest_memory_file *file,
				uint32_t index, void *data)
{
	uint64_t sector;

	if (memory_file_index_valid(file, index) < 0 || !data)
		return -EINVAL;
	if (!file->allocated[index])
		return -ENODATA;
	sector = (uint64_t)(file->first_block + index) * BLOCK_SECTORS;
	return memory_read_sectors(&memory_bdev, data, sector, BLOCK_SECTORS);
}

int ktest_memory_file_write_block(struct ktest_memory_file *file,
				 uint32_t index, const void *data)
{
	uint64_t sector;

	if (memory_file_index_valid(file, index) < 0 || !data)
		return -EINVAL;
	file->allocated[index] = true;
	sector = (uint64_t)(file->first_block + index) * BLOCK_SECTORS;
	return memory_write_sectors(&memory_bdev, data, sector, BLOCK_SECTORS);
}

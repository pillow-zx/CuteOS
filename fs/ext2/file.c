#include "ext2.h"

#include <kernel/buffer.h>
#include <kernel/errno.h>
#include <kernel/string.h>

ssize_t ext2_read_file(struct inode *inode, char *buf, size_t count, loff_t pos)
{
	size_t done = 0;

	if (!inode || !buf)
		return -EINVAL;
	if (pos < 0)
		return -EINVAL;
	if ((uint64_t)pos >= inode->i_size)
		return 0;
	if (count > inode->i_size - (uint64_t)pos)
		count = inode->i_size - (uint64_t)pos;

	while (done < count) {
		uint32_t lblock =
			(uint32_t)(((uint64_t)pos + done) / BLOCK_SIZE);
		uint32_t offset =
			(uint32_t)(((uint64_t)pos + done) % BLOCK_SIZE);
		uint32_t pblock = ext2_bmap(inode, lblock, false);
		size_t chunk = BLOCK_SIZE - offset;

		if (chunk > count - done)
			chunk = count - done;

		if (!pblock) {
			memset(buf + done, 0, chunk);
		} else {
			struct buffer_head *bh =
				bread(inode->i_sb->s_dev, pblock);
			if (!bh)
				return done ? (ssize_t)done : -EIO;
			memcpy(buf + done, bh->b_data + offset, chunk);
			brelse(bh);
		}

		done += chunk;
	}

	return (ssize_t)done;
}

ssize_t ext2_write_file(struct inode *inode, const char *buf, size_t count,
			loff_t pos)
{
	size_t done = 0;

	if (!inode || !buf)
		return -EINVAL;
	if (pos < 0)
		return -EINVAL;

	while (done < count) {
		uint32_t lblock =
			(uint32_t)(((uint64_t)pos + done) / BLOCK_SIZE);
		uint32_t offset =
			(uint32_t)(((uint64_t)pos + done) % BLOCK_SIZE);
		uint32_t pblock = ext2_bmap(inode, lblock, true);
		size_t chunk = BLOCK_SIZE - offset;
		struct buffer_head *bh;

		if (chunk > count - done)
			chunk = count - done;
		if (!pblock)
			return done ? (ssize_t)done : -ENOSPC;

		bh = bread(inode->i_sb->s_dev, pblock);
		if (!bh)
			return done ? (ssize_t)done : -EIO;

		memcpy(bh->b_data + offset, buf + done, chunk);
		if (bwrite(bh) < 0) {
			brelse(bh);
			return done ? (ssize_t)done : -EIO;
		}
		brelse(bh);
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

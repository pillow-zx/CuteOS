#include "ext2.h"

#include <kernel/buffer.h>
#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <kernel/slab.h>
#include <kernel/stat.h>
#include <kernel/string.h>

static int ext2_inode_location(struct inode *inode, uint32_t *block,
			       uint32_t *offset)
{
	struct ext2_sb_info *sbi = EXT2_SB(inode->i_sb);
	uint32_t ino = (uint32_t)inode->i_ino;
	uint32_t group;
	uint32_t index;
	uint32_t byte_offset;

	if (ino == 0)
		return -EINVAL;

	group = (ino - 1) / sbi->s_inodes_per_group;
	index = (ino - 1) % sbi->s_inodes_per_group;
	if (group >= sbi->s_groups_count)
		return -EINVAL;

	byte_offset = index * sbi->s_inode_size;
	*block = sbi->s_group_desc[group].bg_inode_table +
		 byte_offset / BLOCK_SIZE;
	*offset = byte_offset % BLOCK_SIZE;

	return 0;
}

static dev_t ext2_decode_dev(uint32_t raw)
{
	uint32_t major = (raw >> 8) & 0xff;
	uint32_t minor = raw & 0xff;

	return MKDEV(major, minor);
}

static uint32_t ext2_encode_dev(dev_t dev)
{
	return (MAJOR(dev) << 8) | (dev & 0xff);
}

static void ext2_fill_vfs_inode(struct inode *inode)
{
	struct ext2_inode *raw = &EXT2_I(inode)->raw_inode;

	inode->i_mode = raw->i_mode;
	inode->i_uid = raw->i_uid;
	inode->i_gid = raw->i_gid;
	inode->i_nlink = raw->i_links_count;
	inode->i_size = raw->i_size;
	if ((raw->i_mode & EXT2_S_IFMT) == EXT2_S_IFCHR ||
	    (raw->i_mode & EXT2_S_IFMT) == EXT2_S_IFBLK)
		inode->i_rdev = ext2_decode_dev(raw->i_block[0]);

	switch (raw->i_mode & EXT2_S_IFMT) {
	case EXT2_S_IFDIR:
		inode->i_op = &ext2_dir_inode_operations;
		inode->i_fop = &ext2_dir_operations;
		break;
	case EXT2_S_IFLNK:
		inode->i_op = &ext2_symlink_inode_operations;
		break;
	case EXT2_S_IFREG:
	default:
		inode->i_fop = &ext2_file_operations;
		break;
	}
}

/*
 * 读取符号链接目标。EXT2 有两种存储方式：
 *   快速符号链接 (i_blocks == 0)：目标内联存放在 i_block[] 数组（最多
 *                                 60 字节），不占用数据块；
 *   慢速符号链接：目标存放在第一个数据块中。
 * 最多写入 size 字节（不追加结尾 '\0'），返回写入的字节数。
 */
static int ext2_readlink(struct inode *inode, char *buf, size_t size)
{
	struct ext2_inode *raw = &EXT2_I(inode)->raw_inode;
	uint64_t len = inode->i_size;

	if (!buf || size == 0)
		return -EINVAL;
	if (len > size)
		len = size;

	if (raw->i_blocks == 0) {
		if (inode->i_size > sizeof(raw->i_block))
			return -EIO;
		memcpy(buf, raw->i_block, (size_t)len);
	} else {
		uint32_t pblock = ext2_bmap(inode, 0, false);
		struct buffer_head *bh;

		if (!pblock)
			return -EIO;
		bh = bread(inode->i_sb->s_dev, pblock);
		if (!bh)
			return -EIO;
		if (len > BLOCK_SIZE)
			len = BLOCK_SIZE;
		memcpy(buf, bh->b_data, (size_t)len);
		brelse(bh);
	}

	return (int)len;
}

const struct inode_operations ext2_symlink_inode_operations = {
	.readlink = ext2_readlink,
};

int ext2_read_inode(struct inode *inode)
{
	struct ext2_inode_info *ei;
	struct buffer_head *bh;
	uint32_t block;
	uint32_t offset;
	int ret;

	if (!inode || !inode->i_sb)
		return -EINVAL;

	ei = kmalloc(sizeof(*ei));
	if (!ei)
		return -ENOMEM;
	memset(ei, 0, sizeof(*ei));

	ret = ext2_inode_location(inode, &block, &offset);
	if (ret < 0) {
		kfree(ei);
		return ret;
	}

	bh = bread(inode->i_sb->s_dev, block);
	if (!bh) {
		kfree(ei);
		return -EIO;
	}

	memcpy(&ei->raw_inode, bh->b_data + offset, sizeof(ei->raw_inode));
	brelse(bh);

	inode->i_private = ei;
	ext2_fill_vfs_inode(inode);

	return 0;
}

int ext2_write_inode(struct inode *inode)
{
	struct ext2_inode_info *ei;
	struct buffer_head *bh;
	uint32_t block;
	uint32_t offset;
	int ret;

	if (!inode || !inode->i_sb || !inode->i_private)
		return -EINVAL;

	ei = EXT2_I(inode);
	ei->raw_inode.i_mode = (uint16_t)inode->i_mode;
	ei->raw_inode.i_uid = (uint16_t)inode->i_uid;
	ei->raw_inode.i_gid = (uint16_t)inode->i_gid;
	ei->raw_inode.i_links_count = (uint16_t)inode->i_nlink;
	ei->raw_inode.i_size = (uint32_t)inode->i_size;
	if ((inode->i_mode & S_IFMT) == S_IFCHR ||
	    (inode->i_mode & S_IFMT) == S_IFBLK)
		ei->raw_inode.i_block[0] = ext2_encode_dev(inode->i_rdev);

	ret = ext2_inode_location(inode, &block, &offset);
	if (ret < 0)
		return ret;

	bh = bread(inode->i_sb->s_dev, block);
	if (!bh)
		return -EIO;

	memcpy(bh->b_data + offset, &ei->raw_inode, sizeof(ei->raw_inode));
	ret = bwrite(bh);
	brelse(bh);

	return ret;
}

static uint32_t ext2_alloc_bmap_block(struct inode *inode)
{
	struct ext2_inode *raw = &EXT2_I(inode)->raw_inode;
	uint32_t block = ext2_alloc_block(inode);

	if (block)
		raw->i_blocks += BLOCK_SIZE / SECTOR_SIZE;
	return block;
}

static uint32_t ext2_ind_bmap(struct inode *inode, uint32_t ind_block,
			      uint32_t index, bool create)
{
	struct buffer_head *bh;
	uint32_t *blocks;
	uint32_t block;

	if (!ind_block)
		return 0;

	bh = bread(inode->i_sb->s_dev, ind_block);
	if (!bh)
		return 0;

	blocks = (uint32_t *)bh->b_data;
	block = blocks[index];
	if (!block && create) {
		block = ext2_alloc_bmap_block(inode);
		if (block) {
			blocks[index] = block;
			bwrite(bh);
			ext2_write_inode(inode);
		}
	}

	brelse(bh);
	return block;
}

uint32_t ext2_bmap(struct inode *inode, uint32_t block, bool create)
{
	struct ext2_inode *raw;
	uint32_t ptrs = BLOCK_SIZE / sizeof(uint32_t);
	uint32_t first;
	uint32_t second;
	struct buffer_head *bh;
	uint32_t *blocks;

	if (!inode || !inode->i_private)
		return 0;

	raw = &EXT2_I(inode)->raw_inode;
	if (block < EXT2_NDIR_BLOCKS) {
		if (!raw->i_block[block] && create) {
			raw->i_block[block] = ext2_alloc_bmap_block(inode);
			ext2_write_inode(inode);
		}
		return raw->i_block[block];
	}

	block -= EXT2_NDIR_BLOCKS;
	if (block < ptrs) {
		if (!raw->i_block[EXT2_IND_BLOCK] && create) {
			raw->i_block[EXT2_IND_BLOCK] =
				ext2_alloc_bmap_block(inode);
			ext2_write_inode(inode);
		}
		return ext2_ind_bmap(inode, raw->i_block[EXT2_IND_BLOCK], block,
				     create);
	}

	block -= ptrs;
	if (block >= ptrs * ptrs)
		return 0;

	if (!raw->i_block[EXT2_DIND_BLOCK] && create) {
		raw->i_block[EXT2_DIND_BLOCK] = ext2_alloc_bmap_block(inode);
		ext2_write_inode(inode);
	}
	if (!raw->i_block[EXT2_DIND_BLOCK])
		return 0;

	first = block / ptrs;
	second = block % ptrs;
	bh = bread(inode->i_sb->s_dev, raw->i_block[EXT2_DIND_BLOCK]);
	if (!bh)
		return 0;

	blocks = (uint32_t *)bh->b_data;
	if (!blocks[first] && create) {
		blocks[first] = ext2_alloc_bmap_block(inode);
		if (blocks[first])
			bwrite(bh);
	}
	first = blocks[first];
	brelse(bh);

	return ext2_ind_bmap(inode, first, second, create);
}

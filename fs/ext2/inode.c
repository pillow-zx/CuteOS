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

static const struct inode_operations ext2_file_inode_operations;

static void ext2_free_indirect_chain(struct super_block *sb, uint32_t block,
				     int depth)
{
	uint32_t ptrs = BLOCK_SIZE / sizeof(uint32_t);
	struct buffer_head *bh;
	uint32_t *entries;

	if (!block)
		return;
	if (depth == 0) {
		ext2_free_block(sb, block);
		return;
	}

	bh = bread(sb->s_dev, block);
	if (bh) {
		entries = (uint32_t *)bh->b_data;
		for (uint32_t i = 0; i < ptrs; i++) {
			if (!entries[i])
				continue;
			ext2_free_indirect_chain(sb, entries[i], depth - 1);
		}
		brelse(bh);
	}

	ext2_free_block(sb, block);
}

static void ext2_free_inode_blocks(struct inode *inode)
{
	struct ext2_inode *raw = &EXT2_I(inode)->raw_inode;

	for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS; i++) {
		if (raw->i_block[i])
			ext2_free_block(inode->i_sb, raw->i_block[i]);
		raw->i_block[i] = 0;
	}

	ext2_free_indirect_chain(inode->i_sb, raw->i_block[EXT2_IND_BLOCK], 1);
	raw->i_block[EXT2_IND_BLOCK] = 0;
	ext2_free_indirect_chain(inode->i_sb, raw->i_block[EXT2_DIND_BLOCK], 2);
	raw->i_block[EXT2_DIND_BLOCK] = 0;
	ext2_free_indirect_chain(inode->i_sb, raw->i_block[EXT2_TIND_BLOCK], 3);
	raw->i_block[EXT2_TIND_BLOCK] = 0;

	raw->i_blocks = 0;
	raw->i_size = 0;
	inode->i_size = 0;
}

static uint32_t ext2_branch_span(int depth)
{
	uint32_t ptrs = BLOCK_SIZE / sizeof(uint32_t);
	uint32_t span = 1;

	for (int i = 0; i < depth; i++)
		span *= ptrs;
	return span;
}

static uint32_t ext2_count_tree_blocks(struct super_block *sb, uint32_t block,
				       int depth)
{
	uint32_t ptrs = BLOCK_SIZE / sizeof(uint32_t);
	struct buffer_head *bh;
	uint32_t *entries;
	uint32_t total = 1;

	if (!block)
		return 0;
	if (depth == 0)
		return 1;

	bh = bread(sb->s_dev, block);
	if (!bh)
		return total;

	entries = (uint32_t *)bh->b_data;
	for (uint32_t i = 0; i < ptrs; i++) {
		if (!entries[i])
			continue;
		total += ext2_count_tree_blocks(sb, entries[i], depth - 1);
	}
	brelse(bh);
	return total;
}

static int ext2_truncate_branch_slot(struct inode *inode, uint32_t *slot,
				     int depth, uint32_t keep_blocks)
{
	uint32_t ptrs = BLOCK_SIZE / sizeof(uint32_t);
	struct buffer_head *bh;
	uint32_t *entries;
	uint32_t span;
	uint32_t remaining = keep_blocks;
	bool all_zero = true;

	if (!slot || !*slot)
		return 0;
	if (keep_blocks == 0) {
		ext2_free_indirect_chain(inode->i_sb, *slot, depth);
		*slot = 0;
		return 0;
	}
	if (depth == 0)
		return 0;

	span = ext2_branch_span(depth - 1);
	bh = bread(inode->i_sb->s_dev, *slot);
	if (!bh)
		return -EIO;

	entries = (uint32_t *)bh->b_data;
	for (uint32_t i = 0; i < ptrs; i++) {
		uint32_t child_keep = remaining > span ? span : remaining;
		int ret = ext2_truncate_branch_slot(inode, &entries[i],
						    depth - 1, child_keep);

		if (ret < 0) {
			brelse(bh);
			return ret;
		}
		if (entries[i])
			all_zero = false;
		if (remaining > child_keep)
			remaining -= child_keep;
		else
			remaining = 0;
	}

	if (all_zero) {
		brelse(bh);
		ext2_free_block(inode->i_sb, *slot);
		*slot = 0;
		return 0;
	}

	if (bwrite(bh) < 0) {
		brelse(bh);
		return -EIO;
	}
	brelse(bh);
	return 0;
}

static int ext2_truncate_branch(struct inode *inode, uint32_t index, int depth,
				uint32_t keep_blocks)
{
	struct ext2_inode *raw = &EXT2_I(inode)->raw_inode;
	uint32_t slot = raw->i_block[index];
	int ret = ext2_truncate_branch_slot(inode, &slot, depth, keep_blocks);

	if (ret < 0)
		return ret;
	raw->i_block[index] = slot;
	return 0;
}

static int ext2_zero_truncate_tail(struct inode *inode, uint64_t size)
{
	uint32_t offset = (uint32_t)(size % BLOCK_SIZE);
	uint32_t lblock;
	uint32_t pblock;
	struct buffer_head *bh;

	if (size == 0 || offset == 0)
		return 0;

	lblock = (uint32_t)(size / BLOCK_SIZE);
	pblock = ext2_bmap(inode, lblock, false);
	if (!pblock)
		return 0;

	bh = bread(inode->i_sb->s_dev, pblock);
	if (!bh)
		return -EIO;

	memset(bh->b_data + offset, 0, BLOCK_SIZE - offset);
	if (bwrite(bh) < 0) {
		brelse(bh);
		return -EIO;
	}
	brelse(bh);
	return 0;
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
		inode->i_op = &ext2_file_inode_operations;
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
	.truncate = ext2_truncate_inode,
};

static const struct inode_operations ext2_file_inode_operations = {
	.truncate = ext2_truncate_inode,
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

int ext2_truncate_inode(struct inode *inode, uint64_t size)
{
	struct ext2_inode *raw;
	uint32_t keep_blocks;
	uint32_t remaining;
	int ret;

	if (!inode || !inode->i_private)
		return -EINVAL;
	if (size > UINT32_MAX)
		return -EINVAL;
	if (size == inode->i_size)
		return 0;

	raw = &EXT2_I(inode)->raw_inode;
	if (size == 0) {
		ext2_free_inode_blocks(inode);
		return ext2_write_inode(inode);
	}

	if (size < inode->i_size) {
		ret = ext2_zero_truncate_tail(inode, size);
		if (ret < 0)
			return ret;

		keep_blocks = (uint32_t)((size + BLOCK_SIZE - 1) / BLOCK_SIZE);
		remaining = keep_blocks;

		for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS; i++) {
			uint32_t child_keep = remaining ? 1 : 0;

			ret = ext2_truncate_branch(inode, i, 0, child_keep);
			if (ret < 0)
				return ret;
			if (remaining)
				remaining--;
		}

		ret = ext2_truncate_branch(inode, EXT2_IND_BLOCK, 1, remaining);
		if (ret < 0)
			return ret;
		if (remaining > ext2_branch_span(1))
			remaining -= ext2_branch_span(1);
		else
			remaining = 0;

		ret = ext2_truncate_branch(inode, EXT2_DIND_BLOCK, 2,
					   remaining);
		if (ret < 0)
			return ret;
		if (remaining > ext2_branch_span(2))
			remaining -= ext2_branch_span(2);
		else
			remaining = 0;

		ret = ext2_truncate_branch(inode, EXT2_TIND_BLOCK, 3,
					   remaining);
		if (ret < 0)
			return ret;
	}

	inode->i_size = size;
	raw->i_blocks =
		(ext2_count_tree_blocks(inode->i_sb, raw->i_block[0], 0) +
		 ext2_count_tree_blocks(inode->i_sb, raw->i_block[1], 0) +
		 ext2_count_tree_blocks(inode->i_sb, raw->i_block[2], 0) +
		 ext2_count_tree_blocks(inode->i_sb, raw->i_block[3], 0) +
		 ext2_count_tree_blocks(inode->i_sb, raw->i_block[4], 0) +
		 ext2_count_tree_blocks(inode->i_sb, raw->i_block[5], 0) +
		 ext2_count_tree_blocks(inode->i_sb, raw->i_block[6], 0) +
		 ext2_count_tree_blocks(inode->i_sb, raw->i_block[7], 0) +
		 ext2_count_tree_blocks(inode->i_sb, raw->i_block[8], 0) +
		 ext2_count_tree_blocks(inode->i_sb, raw->i_block[9], 0) +
		 ext2_count_tree_blocks(inode->i_sb, raw->i_block[10], 0) +
		 ext2_count_tree_blocks(inode->i_sb, raw->i_block[11], 0) +
		 ext2_count_tree_blocks(inode->i_sb,
					raw->i_block[EXT2_IND_BLOCK], 1) +
		 ext2_count_tree_blocks(inode->i_sb,
					raw->i_block[EXT2_DIND_BLOCK], 2) +
		 ext2_count_tree_blocks(inode->i_sb,
					raw->i_block[EXT2_TIND_BLOCK], 3)) *
		(BLOCK_SIZE / SECTOR_SIZE);

	return ext2_write_inode(inode);
}

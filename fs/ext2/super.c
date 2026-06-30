#include "ext2.h"

#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <kernel/page_cache.h>
#include <kernel/printk.h>
#include <kernel/slab.h>
#include <kernel/statfs.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

#define EXT2_ROOT_DEV MKDEV(8, 0)
static struct super_block *ext2_mount(struct file_system_type *fs_type,
				      dev_t dev, void *data);
static void ext2_evict_inode(struct inode *inode);
static int ext2_statfs(struct super_block *sb, struct kstatfs *buf);

static const struct super_operations ext2_sops = {
	.read_inode = ext2_read_inode,
	.write_inode = ext2_write_inode,
	.evict_inode = ext2_evict_inode,
	.statfs = ext2_statfs,
};

static struct file_system_type ext2_fs_type = {
	.name = "ext2",
	.mount = ext2_mount,
};

static void ext2_evict_inode(struct inode *inode)
{
	if (!inode)
		return;

	page_cache_invalidate_inode(inode);
	if (inode->i_nlink == 0 && inode->i_private) {
		ext2_free_inode_blocks(inode);
		ext2_free_inode(inode->i_sb, (uint32_t)inode->i_ino);
	}
	kfree(inode->i_private);
	inode->i_private = NULL;
}

static void ext2_free_sbi(struct ext2_sb_info *sbi)
{
	if (!sbi)
		return;

	kfree(sbi->s_group_desc);
	kfree(sbi);
}

static void ext2_free_super(struct super_block *sb)
{
	if (!sb)
		return;

	ext2_free_sbi(EXT2_SB(sb));
	kfree(sb);
}

static int ext2_statfs(struct super_block *sb, struct kstatfs *buf)
{
	struct ext2_sb_info *sbi;
	uint64_t free_blocks = 0;
	uint64_t free_inodes = 0;

	if (!sb || !buf)
		return -EINVAL;
	sbi = EXT2_SB(sb);
	if (!sbi)
		return -EINVAL;

	for (uint32_t i = 0; i < sbi->s_groups_count; i++) {
		free_blocks += sbi->s_group_desc[i].bg_free_blocks_count;
		free_inodes += sbi->s_group_desc[i].bg_free_inodes_count;
	}

	memset(buf, 0, sizeof(*buf));
	buf->f_type = EXT2_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = sbi->s_es.s_blocks_count;
	buf->f_bfree = free_blocks;
	buf->f_bavail = free_blocks;
	buf->f_files = sbi->s_es.s_inodes_count;
	buf->f_ffree = free_inodes;
	buf->f_fsid[0] = (int32_t)sb->s_dev;
	buf->f_fsid[1] = 0;
	buf->f_namelen = EXT2_NAME_LEN;
	buf->f_frsize = sb->s_blocksize;
	buf->f_flags = sb->s_flags;
	return 0;
}

static uint32_t div_round_up_u32(uint32_t value, uint32_t divisor)
{
	return (value + divisor - 1) / divisor;
}

static int ext2_read_bgdt(struct super_block *sb)
{
	struct ext2_sb_info *sbi = EXT2_SB(sb);
	uint32_t desc_per_block = BLOCK_SIZE / sizeof(struct ext2_group_desc);
	uint32_t bytes = sbi->s_groups_count * sizeof(struct ext2_group_desc);
	uint32_t first_block = EXT2_BGDT_BLOCK(sbi->s_first_data_block);
	uint8_t *dst;

	if (bytes > 2048)
		return -ENOMEM;

	sbi->s_group_desc = kmalloc(bytes);
	if (!sbi->s_group_desc)
		return -ENOMEM;

	dst = (uint8_t *)sbi->s_group_desc;
	for (uint32_t block = 0;
	     block < div_round_up_u32(sbi->s_groups_count, desc_per_block);
	     block++) {
		struct page_cache *page =
			page_cache_get_block(sb->s_dev, first_block + block);
		uint32_t copy = bytes - block * BLOCK_SIZE;

		if (!page) {
			kfree(sbi->s_group_desc);
			sbi->s_group_desc = NULL;
			return -EIO;
		}
		if (copy > BLOCK_SIZE)
			copy = BLOCK_SIZE;

		memcpy(dst + block * BLOCK_SIZE, page_cache_data(page), copy);
		page_cache_put_page(page);
	}

	return 0;
}

static int ext2_read_super(struct super_block *sb)
{
	struct ext2_sb_info *sbi;
	struct page_cache *page;
	uint32_t super_block;
	uint32_t super_off;
	uint32_t block_size;
	int ret;

	sbi = kmalloc(sizeof(*sbi));
	if (!sbi)
		return -ENOMEM;
	memset(sbi, 0, sizeof(*sbi));

	super_block = ext2_super_blocknr(BLOCK_SIZE);
	super_off = ext2_super_offset(BLOCK_SIZE);
	page = page_cache_get_block(sb->s_dev, super_block);
	if (!page) {
		kfree(sbi);
		return -EIO;
	}

	memcpy(&sbi->s_es, page_cache_data(page) + super_off,
	       sizeof(sbi->s_es));
	page_cache_put_page(page);

	if (sbi->s_es.s_magic != EXT2_SUPER_MAGIC) {
		kfree(sbi);
		return -EINVAL;
	}

	block_size = 1024u << sbi->s_es.s_log_block_size;
	if (block_size != BLOCK_SIZE) {
		kfree(sbi);
		return -EINVAL;
	}

	sbi->s_inode_size = sbi->s_es.s_rev_level == EXT2_GOOD_OLD_REV
				    ? EXT2_GOOD_OLD_INODE_SIZE
				    : sbi->s_es.s_inode_size;
	if (sbi->s_inode_size < sizeof(struct ext2_inode)) {
		kfree(sbi);
		return -EINVAL;
	}

	sbi->s_blocks_per_group = sbi->s_es.s_blocks_per_group;
	sbi->s_inodes_per_group = sbi->s_es.s_inodes_per_group;
	sbi->s_first_data_block = sbi->s_es.s_first_data_block;
	if (!sbi->s_blocks_per_group || !sbi->s_inodes_per_group) {
		kfree(sbi);
		return -EINVAL;
	}

	sbi->s_groups_count = div_round_up_u32(sbi->s_es.s_blocks_count -
						       sbi->s_first_data_block,
					       sbi->s_blocks_per_group);

	sb->s_blocksize = BLOCK_SIZE;
	sb->s_op = &ext2_sops;
	sb->s_private = sbi;

	ret = ext2_read_bgdt(sb);
	if (ret < 0) {
		ext2_free_sbi(sbi);
		sb->s_private = NULL;
		return ret;
	}

	return 0;
}

static struct super_block *ext2_mount(struct file_system_type *fs_type,
				      dev_t dev, void *data)
{
	struct super_block *sb;
	struct inode *root_inode;
	struct dentry *root;
	int ret;

	(void)data;

	sb = super_alloc(fs_type, dev);
	if (!sb)
		return NULL;

	ret = ext2_read_super(sb);
	if (ret < 0) {
		ext2_free_super(sb);
		return NULL;
	}

	root = dentry_alloc(NULL, "/", 1);
	if (!root) {
		ext2_free_super(sb);
		return NULL;
	}

	root_inode = iget(sb, EXT2_ROOT_INO);
	if (!root_inode) {
		kfree(root);
		ext2_free_super(sb);
		return NULL;
	}

	root->d_inode = root_inode;
	root->d_sb = sb;
	root->d_parent = root;
	sb->s_root = root;

	return sb;
}

int ext2_init(void)
{
	return register_filesystem(&ext2_fs_type);
}

int mount_root(void)
{
	struct file_system_type *fs_type;
	struct super_block *sb;
	int ret;

	fs_type = get_filesystem_type("ext2");
	if (!fs_type) {
		ret = ext2_init();
		if (ret < 0)
			return ret;
		fs_type = get_filesystem_type("ext2");
	}

	if (!fs_type || !fs_type->mount)
		return -EINVAL;

	sb = fs_type->mount(fs_type, EXT2_ROOT_DEV, NULL);
	if (!sb)
		return -EINVAL;

	vfs_set_root_dentry(sb->s_root);
	ret = vfs_mount_root(sb->s_root);
	if (ret < 0)
		return ret;
	pr_info("VFS: mounted root (ext2)\n");
	return 0;
}

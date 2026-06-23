#include "ext2.h"

#include <kernel/errno.h>
#include <kernel/page_cache.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

#define EXT2_DIR_REC_LEN(name_len) (((name_len) + 8 + 3) & ~3u)

static uint8_t ext2_file_type(uint16_t mode)
{
	switch (mode & EXT2_S_IFMT) {
	case EXT2_S_IFDIR:
		return EXT2_FT_DIR;
	case EXT2_S_IFCHR:
		return EXT2_FT_CHRDEV;
	case EXT2_S_IFBLK:
		return EXT2_FT_BLKDEV;
	case EXT2_S_IFIFO:
		return EXT2_FT_FIFO;
	case EXT2_S_IFLNK:
		return EXT2_FT_SYMLINK;
	case EXT2_S_IFSOCK:
		return EXT2_FT_SOCK;
	case EXT2_S_IFREG:
	default:
		return EXT2_FT_REG_FILE;
	}
}

static bool ext2_match(struct ext2_dir_entry_2 *de, const char *name,
		       size_t namelen)
{
	if (!de->inode || de->name_len != namelen)
		return false;
	return memcmp(de->name, name, namelen) == 0;
}

static struct ext2_dir_entry_2 *ext2_find_entry(struct inode *dir,
						const char *name,
						size_t namelen,
						struct page_cache_page **res_page)
{
	uint32_t blocks =
		(uint32_t)((dir->i_size + BLOCK_SIZE - 1) / BLOCK_SIZE);

	for (uint32_t lblock = 0; lblock < blocks; lblock++) {
		uint32_t pblock = ext2_bmap(dir, lblock, false);
		struct page_cache_page *page;
		uint8_t *data;
		uint32_t offset = 0;

		if (!pblock)
			continue;
		page = page_cache_get_block(dir->i_sb->s_dev, pblock);
		if (!page)
			continue;
		data = page_cache_data(page);

		while (offset + 8 <= BLOCK_SIZE) {
			struct ext2_dir_entry_2 *de =
				(struct ext2_dir_entry_2 *)(data + offset);

			if (de->rec_len < 8 ||
			    offset + de->rec_len > BLOCK_SIZE)
				break;
			if (ext2_match(de, name, namelen)) {
				*res_page = page;
				return de;
			}
			offset += de->rec_len;
		}
		page_cache_put_page(page);
	}

	return NULL;
}

static int ext2_add_entry(struct inode *dir, const char *name, size_t namelen,
			  uint32_t ino, uint8_t type)
{
	uint16_t need = EXT2_DIR_REC_LEN(namelen);
	uint32_t blocks =
		(uint32_t)((dir->i_size + BLOCK_SIZE - 1) / BLOCK_SIZE);

	for (uint32_t lblock = 0; lblock < blocks; lblock++) {
		uint32_t pblock = ext2_bmap(dir, lblock, false);
		struct page_cache_page *page;
		uint8_t *data;
		uint32_t offset = 0;

		if (!pblock)
			continue;
		page = page_cache_get_block(dir->i_sb->s_dev, pblock);
		if (!page)
			return -EIO;
		data = page_cache_data(page);

		while (offset + 8 <= BLOCK_SIZE) {
			struct ext2_dir_entry_2 *de =
				(struct ext2_dir_entry_2 *)(data + offset);
			uint16_t used;
			uint16_t spare;

			if (de->rec_len < 8 ||
			    offset + de->rec_len > BLOCK_SIZE)
				break;

			if (!de->inode && de->rec_len >= need) {
				de->inode = ino;
				de->name_len = (uint8_t)namelen;
				de->file_type = type;
				memcpy(de->name, name, namelen);
				page_cache_sync_block(page);
				page_cache_put_page(page);
				return 0;
			}

			used = EXT2_DIR_REC_LEN(de->name_len);
			spare = de->rec_len - used;
			if (spare >= need) {
				struct ext2_dir_entry_2 *new_de;

				de->rec_len = used;
				new_de =
					(struct ext2_dir_entry_2 *)((uint8_t *)
									    de +
								    used);
				new_de->inode = ino;
				new_de->rec_len = spare;
				new_de->name_len = (uint8_t)namelen;
				new_de->file_type = type;
				memcpy(new_de->name, name, namelen);
				page_cache_sync_block(page);
				page_cache_put_page(page);
				return 0;
			}

			offset += de->rec_len;
		}
		page_cache_put_page(page);
	}

	uint32_t new_block = ext2_bmap(dir, blocks, true);
	if (!new_block)
		return -ENOSPC;

	struct page_cache_page *page =
		page_cache_get_block(dir->i_sb->s_dev, new_block);
	if (!page)
		return -EIO;

	uint8_t *data = page_cache_data(page);
	memset(data, 0, BLOCK_SIZE);
	struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)data;
	de->inode = ino;
	de->rec_len = BLOCK_SIZE;
	de->name_len = (uint8_t)namelen;
	de->file_type = type;
	memcpy(de->name, name, namelen);
	page_cache_sync_block(page);
	page_cache_put_page(page);

	dir->i_size += BLOCK_SIZE;
	ext2_write_inode(dir);
	return 0;
}

static int ext2_delete_entry(struct inode *dir, struct dentry *dentry)
{
	uint32_t blocks =
		(uint32_t)((dir->i_size + BLOCK_SIZE - 1) / BLOCK_SIZE);

	for (uint32_t lblock = 0; lblock < blocks; lblock++) {
		uint32_t pblock = ext2_bmap(dir, lblock, false);
		struct page_cache_page *page;
		struct ext2_dir_entry_2 *prev = NULL;
		uint8_t *data;
		uint32_t offset = 0;

		if (!pblock)
			continue;
		page = page_cache_get_block(dir->i_sb->s_dev, pblock);
		if (!page)
			return -EIO;
		data = page_cache_data(page);

		while (offset + 8 <= BLOCK_SIZE) {
			struct ext2_dir_entry_2 *de =
				(struct ext2_dir_entry_2 *)(data + offset);

			if (de->rec_len < 8 ||
			    offset + de->rec_len > BLOCK_SIZE)
				break;
			if (ext2_match(de, dentry->d_name, dentry->d_namelen)) {
				if (prev)
					prev->rec_len += de->rec_len;
				else
					de->inode = 0;
				page_cache_sync_block(page);
				page_cache_put_page(page);
				return 0;
			}
			prev = de;
			offset += de->rec_len;
		}
		page_cache_put_page(page);
	}

	return -ENOENT;
}

static void ext2_rollback_new_inode(struct inode *inode)
{
	if (!inode)
		return;

	inode->i_nlink = 0;
	inode_forget(inode);
}

static struct dentry *ext2_lookup(struct inode *dir, struct dentry *dentry)
{
	struct page_cache_page *page = NULL;
	struct ext2_dir_entry_2 *de;
	struct inode *inode;

	if (!dir || !dentry)
		return NULL;
	if ((dir->i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
		return NULL;

	de = ext2_find_entry(dir, dentry->d_name, dentry->d_namelen, &page);
	if (!de)
		return NULL;

	inode = iget(dir->i_sb, de->inode);
	page_cache_put_page(page);
	if (!inode)
		return NULL;

	dentry->d_inode = inode;
	dentry->d_sb = dir->i_sb;
	return dentry;
}

static int ext2_create(struct inode *dir, struct dentry *dentry, uint32_t mode)
{
	struct page_cache_page *page = NULL;
	uint32_t ino;
	uint32_t type = mode & EXT2_S_IFMT;
	uint32_t inode_mode;
	struct inode *inode;
	struct ext2_inode_info *ei;
	int ret;

	if (ext2_find_entry(dir, dentry->d_name, dentry->d_namelen, &page)) {
		page_cache_put_page(page);
		return -EEXIST;
	}

	if (type == 0)
		type = EXT2_S_IFREG;
	inode_mode = type | (mode & ~EXT2_S_IFMT);

	ino = ext2_alloc_inode(dir->i_sb, (uint16_t)inode_mode);
	if (!ino)
		return -ENOSPC;

	inode = iget(dir->i_sb, ino);
	if (!inode) {
		ext2_free_inode(dir->i_sb, ino);
		return -EIO;
	}

	ei = EXT2_I(inode);
	memset(&ei->raw_inode, 0, sizeof(ei->raw_inode));
	inode->i_mode = inode_mode;
	inode->i_nlink = 1;
	inode->i_size = 0;
	inode->i_rdev = 0;
	ext2_init_inode_ops(inode);
	ext2_write_inode(inode);

	ret = ext2_add_entry(dir, dentry->d_name, dentry->d_namelen, ino,
			     ext2_file_type((uint16_t)inode->i_mode));
	if (ret < 0) {
		ext2_rollback_new_inode(inode);
		return ret;
	}

	dentry->d_inode = inode;
	dentry->d_sb = dir->i_sb;
	return 0;
}

static int ext2_make_empty_dir(struct inode *inode, struct inode *parent)
{
	uint32_t block = ext2_bmap(inode, 0, true);
	struct page_cache_page *page;
	struct ext2_dir_entry_2 *de;
	uint8_t *data;

	if (!block)
		return -ENOSPC;

	page = page_cache_get_block(inode->i_sb->s_dev, block);
	if (!page)
		return -EIO;
	data = page_cache_data(page);

	memset(data, 0, BLOCK_SIZE);
	de = (struct ext2_dir_entry_2 *)data;
	de->inode = (uint32_t)inode->i_ino;
	de->rec_len = EXT2_DIR_REC_LEN(1);
	de->name_len = 1;
	de->file_type = EXT2_FT_DIR;
	de->name[0] = '.';

	de = (struct ext2_dir_entry_2 *)(data + de->rec_len);
	de->inode = (uint32_t)parent->i_ino;
	de->rec_len = BLOCK_SIZE - EXT2_DIR_REC_LEN(1);
	de->name_len = 2;
	de->file_type = EXT2_FT_DIR;
	de->name[0] = '.';
	de->name[1] = '.';

	page_cache_sync_block(page);
	page_cache_put_page(page);
	inode->i_size = BLOCK_SIZE;
	ext2_write_inode(inode);
	return 0;
}

static int ext2_mkdir(struct inode *dir, struct dentry *dentry, uint32_t mode)
{
	struct page_cache_page *page = NULL;
	uint32_t ino;
	struct inode *inode;
	struct ext2_inode_info *ei;
	int ret;

	if (ext2_find_entry(dir, dentry->d_name, dentry->d_namelen, &page)) {
		page_cache_put_page(page);
		return -EEXIST;
	}

	ino = ext2_alloc_inode(dir->i_sb, (uint16_t)(EXT2_S_IFDIR | mode));
	if (!ino)
		return -ENOSPC;

	inode = iget(dir->i_sb, ino);
	if (!inode) {
		ext2_free_inode(dir->i_sb, ino);
		return -EIO;
	}

	ei = EXT2_I(inode);
	memset(&ei->raw_inode, 0, sizeof(ei->raw_inode));
	inode->i_mode = EXT2_S_IFDIR | mode;
	inode->i_nlink = 2;
	inode->i_op = &ext2_dir_inode_operations;
	inode->i_fop = &ext2_dir_operations;
	ext2_write_inode(inode);

	ret = ext2_make_empty_dir(inode, dir);
	if (ret < 0) {
		ext2_rollback_new_inode(inode);
		return ret;
	}

	ret = ext2_add_entry(dir, dentry->d_name, dentry->d_namelen, ino,
			     ext2_file_type((uint16_t)inode->i_mode));
	if (ret < 0) {
		ext2_rollback_new_inode(inode);
		return ret;
	}

	dir->i_nlink++;
	ext2_write_inode(dir);
	dentry->d_inode = inode;
	dentry->d_sb = dir->i_sb;
	return 0;
}

static bool ext2_dir_is_empty(struct inode *inode)
{
	uint32_t blocks =
		(uint32_t)((inode->i_size + BLOCK_SIZE - 1) / BLOCK_SIZE);

	for (uint32_t lblock = 0; lblock < blocks; lblock++) {
		uint32_t pblock = ext2_bmap(inode, lblock, false);
		struct page_cache_page *page;
		uint8_t *data;
		uint32_t offset = 0;

		if (!pblock)
			continue;
		page = page_cache_get_block(inode->i_sb->s_dev, pblock);
		if (!page)
			return false;
		data = page_cache_data(page);

		while (offset + 8 <= BLOCK_SIZE) {
			struct ext2_dir_entry_2 *de =
				(struct ext2_dir_entry_2 *)(data + offset);
			bool dot;

			if (de->rec_len < 8 ||
			    offset + de->rec_len > BLOCK_SIZE)
				break;
			dot = (de->name_len == 1 && de->name[0] == '.') ||
			      (de->name_len == 2 && de->name[0] == '.' &&
			       de->name[1] == '.');
			if (de->inode && !dot) {
				page_cache_put_page(page);
				return false;
			}
			offset += de->rec_len;
		}
		page_cache_put_page(page);
	}

	return true;
}

static int ext2_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int ret;

	if (!inode)
		return -ENOENT;
	if ((inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
		return -EISDIR;

	ret = ext2_delete_entry(dir, dentry);
	if (ret < 0)
		return ret;

	if (inode->i_nlink > 0)
		inode->i_nlink--;
	ext2_write_inode(inode);
	dentry->d_inode = NULL;
	iput(inode);
	return 0;
}

static int ext2_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int ret;

	if (!inode)
		return -ENOENT;
	if ((inode->i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
		return -ENOTDIR;
	if (!ext2_dir_is_empty(inode))
		return -ENOTEMPTY;

	ret = ext2_delete_entry(dir, dentry);
	if (ret < 0)
		return ret;

	if (dir->i_nlink > 0)
		dir->i_nlink--;
	inode->i_nlink = 0;
	ext2_write_inode(inode);
	ext2_write_inode(dir);
	dentry->d_inode = NULL;
	iput(inode);
	return 0;
}

static int ext2_readdir(struct file *file, void *ctx, filldir_t filldir)
{
	struct inode *dir = file->f_inode;

	while ((uint64_t)file->f_pos < dir->i_size) {
		uint32_t lblock =
			(uint32_t)((uint64_t)file->f_pos / BLOCK_SIZE);
		uint32_t offset =
			(uint32_t)((uint64_t)file->f_pos % BLOCK_SIZE);
		uint32_t pblock = ext2_bmap(dir, lblock, false);
		struct page_cache_page *page;
		struct ext2_dir_entry_2 *de;
		uint8_t *data;

		if (!pblock) {
			file->f_pos = (loff_t)((lblock + 1) * BLOCK_SIZE);
			continue;
		}

		page = page_cache_get_block(dir->i_sb->s_dev, pblock);
		if (!page)
			return -EIO;
		data = page_cache_data(page);

		de = (struct ext2_dir_entry_2 *)(data + offset);
		if (offset + 8 > BLOCK_SIZE || de->rec_len < 8 ||
		    offset + de->rec_len > BLOCK_SIZE) {
			page_cache_put_page(page);
			return -EIO;
		}

		loff_t next_pos = file->f_pos + de->rec_len;
		if (de->inode && filldir(ctx, de->name, de->name_len, de->inode,
					 de->file_type) < 0) {
			page_cache_put_page(page);
			return 0;
		}

		file->f_pos = next_pos;
		page_cache_put_page(page);
	}

	return 0;
}

const struct inode_operations ext2_dir_inode_operations = {
	.lookup = ext2_lookup,
	.create = ext2_create,
	.unlink = ext2_unlink,
	.mkdir = ext2_mkdir,
	.rmdir = ext2_rmdir,
	.truncate = ext2_truncate_inode,
};

const struct file_operations ext2_dir_operations = {
	.readdir = ext2_readdir,
};

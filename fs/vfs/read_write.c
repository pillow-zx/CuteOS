/*
 * fs/vfs/read_write.c - VFS 读写入口
 *
 * 功能：
 *   提供 VFS 层统一的读写入口点。vfs_read 和 vfs_write 首先检查
 *   f_mode 中的权限位（可读/可写），然后调用 f_op->read/write 分发
 *   到底层具体文件系统实现。每次操作后更新 f_pos 读写位置。
 *
 * 主要函数：
 *   vfs_read(file, buf, count)  - 读入口：检查 f_mode 权限，
 *                                 调用 f_op->read，更新 f_pos
 *   vfs_write(file, buf, count) - 写入口：检查 f_mode 权限，
 *                                 调用 f_op->write，更新 f_pos
 */

#include <kernel/errno.h>
#include <kernel/fs.h>
#include <kernel/stat.h>
#include <kernel/string.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int vfs_inode_writeback(struct inode *inode)
{
	if (!inode || !inode->i_sb || !inode->i_sb->s_op ||
	    !inode->i_sb->s_op->write_inode)
		return -EINVAL;

	return inode->i_sb->s_op->write_inode(inode);
}

int vfs_sync_file(struct file *file)
{
	struct super_block *sb;

	if (!file || !file->f_inode)
		return -EINVAL;

	sb = file->f_inode->i_sb;
	if (!sb || !sb->s_op || !sb->s_op->sync_fs)
		return vfs_inode_writeback(file->f_inode);

	return sb->s_op->sync_fs(sb);
}

int vfs_inode_truncate(struct inode *inode, uint64_t size)
{
	if (!inode || !inode->i_op || !inode->i_op->truncate)
		return -EINVAL;

	return inode->i_op->truncate(inode, size);
}

int vfs_truncate_file(struct file *file, uint64_t size)
{
	if (!file || !file->f_inode)
		return -EINVAL;

	return vfs_inode_truncate(file->f_inode, size);
}

int vfs_stat_inode(const struct inode *inode, struct kstat *st)
{
	uint64_t size;

	if (!st)
		return -EINVAL;

	memset(st, 0, sizeof(*st));
	if (!inode)
		return 0;

	size = vfs_inode_size(inode);
	st->st_dev = vfs_inode_dev(inode);
	st->st_ino = vfs_inode_number(inode);
	st->st_mode = vfs_inode_mode(inode);
	st->st_nlink = vfs_inode_nlink(inode);
	st->st_uid = vfs_inode_uid(inode);
	st->st_gid = vfs_inode_gid(inode);
	st->st_rdev = vfs_inode_rdev(inode);
	st->st_size = (int64_t)size;
	st->st_blksize = 1024;
	st->st_blocks = size / 512 + (size % 512 ? 1 : 0);
	return 0;
}

int vfs_stat_file(struct file *file, struct kstat *st)
{
	if (!file)
		return -EINVAL;

	return vfs_stat_inode(file->f_inode, st);
}

uint64_t vfs_inode_size(const struct inode *inode)
{
	return inode ? inode->i_size : 0;
}

uint64_t vfs_inode_number(const struct inode *inode)
{
	return inode ? inode->i_ino : 0;
}

uint32_t vfs_inode_mode(const struct inode *inode)
{
	return inode ? inode->i_mode : 0;
}

dev_t vfs_inode_rdev(const struct inode *inode)
{
	return inode ? inode->i_rdev : 0;
}

uint32_t vfs_inode_uid(const struct inode *inode)
{
	return inode ? inode->i_uid : 0;
}

uint32_t vfs_inode_gid(const struct inode *inode)
{
	return inode ? inode->i_gid : 0;
}

uint32_t vfs_inode_nlink(const struct inode *inode)
{
	return inode ? inode->i_nlink : 0;
}

dev_t vfs_inode_dev(const struct inode *inode)
{
	return inode && inode->i_sb ? inode->i_sb->s_dev : 0;
}

struct inode *vfs_dentry_inode(struct dentry *dentry)
{
	return dentry ? dentry->d_inode : NULL;
}

struct dentry *vfs_dentry_parent(struct dentry *dentry)
{
	return dentry ? dentry->d_parent : NULL;
}

const char *vfs_dentry_name(struct dentry *dentry)
{
	return dentry ? dentry->d_name : NULL;
}

size_t vfs_dentry_namelen(struct dentry *dentry)
{
	return dentry ? dentry->d_namelen : 0;
}

ssize_t vfs_read(struct file *file, char *buf, size_t count)
{
	if (!file || !(file->f_mode & FMODE_READ) || !file->f_op ||
	    !file->f_op->read)
		return -EBADF;

	ssize_t ret = file->f_op->read(file, buf, count);
	if (ret > 0)
		file->f_pos += ret;

	return ret;
}

ssize_t vfs_write(struct file *file, const char *buf, size_t count)
{
	if (!file || !(file->f_mode & FMODE_WRITE) || !file->f_op ||
	    !file->f_op->write)
		return -EBADF;

	if ((file->f_flags & O_APPEND) && file->f_inode)
		file->f_pos = (loff_t)file->f_inode->i_size;

	ssize_t ret = file->f_op->write(file, buf, count);
	if (ret > 0)
		file->f_pos += ret;

	return ret;
}

loff_t vfs_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t base;

	if (!file)
		return -EBADF;

	if (file->f_op && file->f_op->llseek)
		return file->f_op->llseek(file, offset, whence);

	switch (whence) {
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_CUR:
		base = file->f_pos;
		break;
	case SEEK_END:
		if (!file->f_inode)
			return -ESPIPE;
		base = (loff_t)file->f_inode->i_size;
		break;
	default:
		return -EINVAL;
	}

	if (offset < 0 && base < -offset)
		return -EINVAL;

	file->f_pos = base + offset;
	return file->f_pos;
}

int vfs_readdir(struct file *file, void *ctx, filldir_t filldir)
{
	if (!file || !file->f_op || !file->f_op->readdir)
		return -EBADF;
	if (!filldir)
		return -EINVAL;

	return file->f_op->readdir(file, ctx, filldir);
}

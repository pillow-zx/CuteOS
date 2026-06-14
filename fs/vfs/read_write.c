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

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

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

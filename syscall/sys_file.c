/*
 * syscall/sys_file.c - 文件相关系统调用
 *
 * 功能：
 *   实现文件 I/O 和文件系统操作的系统调用。这些是应用层使用最频繁的
 *   syscall 类别之一，涵盖文件打开/关闭/读写、目录操作、文件描述符
 *   管理等功能。
 *
 * 主要函数：
 *   sys_openat(dfd, path, flags, mode) - 打开文件（仅支持 AT_FDCWD）
 *   sys_close(fd)                      - 关闭文件描述符
 *   sys_read(fd, buf, count)           - 从文件描述符读取数据
 *   sys_write(fd, buf, count)          - 向文件描述符写入数据
 *   sys_lseek(fd, offset, whence)      - 定位读写位置（SET/CUR/END）
 *   sys_ioctl(fd, cmd, arg)            - 设备控制（shell 特殊：直接返回 0）
 *   sys_mkdirat(dfd, path, mode)       - 创建目录
 *   sys_unlinkat(dfd, path, flags)     - 删除文件/目录
 *   sys_chdir(path)                    - 切换当前工作目录
 *   sys_getcwd(buf, size)              - 获取当前工作目录（沿 d_parent 回溯）
 *   sys_getdents64(fd, dirp, count)    - 读取目录条目（使用 filldir 回调）
 *   sys_fstat(fd, statbuf)             - 获取文件状态
 *   sys_dup(oldfd)                     - 复制文件描述符
 *   sys_dup2(oldfd, newfd)             - 复制到指定文件描述符
 *   sys_mknod(path, mode, dev)         - 创建设备节点
 */

#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/stat.h>
#include <kernel/types.h>
#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/vfs.h>
#include <asm/trap.h>

/* 内核临时缓冲区大小，sys_write 分块拷贝使用 */
#define WRITE_BUF_SIZE 256
#define DIRENT_BUF_SIZE 512
#define PATH_BUF_SIZE  256

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define TCGETS 0x5401

struct linux_dirent64 {
	uint64_t d_ino;
	int64_t d_off;
	uint16_t d_reclen;
	uint8_t d_type;
	char d_name[];
};

struct getdents_ctx {
	char *dirp;
	size_t count;
	size_t written;
	loff_t pos;
};

static uint8_t vfs_type_to_dirent(uint8_t type)
{
	switch (type) {
	case 1:
		return DT_REG;
	case 2:
		return DT_DIR;
	case 3:
		return DT_CHR;
	case 4:
		return DT_BLK;
	case 5:
		return DT_FIFO;
	case 6:
		return DT_SOCK;
	case 7:
		return DT_LNK;
	default:
		return DT_UNKNOWN;
	}
}

static int copy_user_path(char *dst, const char *user)
{
	if (!user)
		return -EFAULT;
	if (!access_ok(user, PATH_BUF_SIZE))
		return -EFAULT;

	bool had_sum = user_access_begin();
	for (size_t i = 0; i < PATH_BUF_SIZE; i++) {
		char c = user[i];
		dst[i] = c;
		if (c == '\0') {
			user_access_end(had_sum);
			return i == 0 ? -ENOENT : 0;
		}
	}
	user_access_end(had_sum);

	dst[PATH_BUF_SIZE - 1] = '\0';
	return -ENAMETOOLONG;
}

static void fill_kstat(struct kstat *st, struct inode *inode)
{
	memset(st, 0, sizeof(*st));
	if (!inode)
		return;

	st->st_dev = inode->i_sb ? inode->i_sb->s_dev : 0;
	st->st_ino = inode->i_ino;
	st->st_mode = inode->i_mode;
	st->st_nlink = inode->i_nlink;
	st->st_uid = inode->i_uid;
	st->st_gid = inode->i_gid;
	st->st_rdev = inode->i_rdev;
	st->st_size = (int64_t)inode->i_size;
	st->st_blksize = 1024;
	st->st_blocks = (inode->i_size + 511) / 512;
}

static int getcwd_build(char *buf, size_t size)
{
	struct dentry *stack[32];
	struct dentry *dentry = current ? current->cwd : NULL;
	size_t depth = 0;
	size_t pos = 0;

	if (!dentry || !root_dentry)
		return -ENOENT;

	while (dentry && dentry != root_dentry) {
		if (depth >= 32)
			return -ENAMETOOLONG;
		stack[depth++] = dentry;
		dentry = dentry->d_parent;
	}

	if (size < 2)
		return -ERANGE;

	buf[pos++] = '/';
	for (size_t i = depth; i > 0; i--) {
		struct dentry *entry = stack[i - 1];

		if (pos != 1)
			buf[pos++] = '/';
		if (pos + entry->d_namelen + 1 > size)
			return -ERANGE;
		memcpy(buf + pos, entry->d_name, entry->d_namelen);
		pos += entry->d_namelen;
	}

	buf[pos] = '\0';
	return (int)pos + 1;
}

static int filldir64(void *arg, const char *name, size_t namelen,
		     uint64_t ino, uint8_t type)
{
	struct getdents_ctx *ctx = arg;
	size_t reclen;
	struct linux_dirent64 *dirent;

	if (namelen > VFS_NAME_MAX)
		return -EINVAL;

	reclen = sizeof(struct linux_dirent64) + namelen + 1;
	reclen = (reclen + 7) & ~7UL;
	if (ctx->written + reclen > ctx->count)
		return -EINVAL;

	dirent = (struct linux_dirent64 *)(ctx->dirp + ctx->written);
	dirent->d_ino = ino;
	dirent->d_off = ctx->pos;
	dirent->d_reclen = (uint16_t)reclen;
	dirent->d_type = vfs_type_to_dirent(type);
	memcpy(dirent->d_name, name, namelen);
	dirent->d_name[namelen] = '\0';

	ctx->written += reclen;
	return 0;
}

ssize_t sys_openat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	uint32_t flags = (uint32_t)tf->a2;
	uint32_t mode = (uint32_t)tf->a3;
	char path[PATH_BUF_SIZE];
	int ret;

	if (dfd != AT_FDCWD)
		return -EINVAL;

	ret = copy_user_path(path, upath);
	if (ret < 0)
		return ret;

	return vfs_open(path, flags, mode);
}

ssize_t sys_write(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const char *buf = (const char *)tf->a1;
	size_t len = tf->a2;

	struct file *file = fd_get(fd);
	if (!file || !(file->f_mode & FMODE_WRITE) || !file->f_op ||
	    !file->f_op->write)
		return -EBADF;

	/* 校验用户地址范围 */
	if (!access_ok(buf, len))
		return -EFAULT;

	char kbuf[DIRENT_BUF_SIZE];
	size_t written = 0;

	while (written < len) {
		size_t chunk = len - written;
		if (chunk > WRITE_BUF_SIZE)
			chunk = WRITE_BUF_SIZE;

		if (copy_from_user(kbuf, buf + written, chunk) != 0)
			return -EFAULT;

		ssize_t ret = vfs_write(file, kbuf, chunk);
		if (ret < 0)
			return ret;
		if ((size_t)ret != chunk)
			return (ssize_t)(written + (size_t)ret);

		written += chunk;
	}

	return (ssize_t)written;
}

ssize_t sys_read(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	char *buf = (char *)tf->a1;
	size_t len = tf->a2;

	struct file *file = fd_get(fd);
	if (!file || !(file->f_mode & FMODE_READ) || !file->f_op ||
	    !file->f_op->read)
		return -EBADF;

	if (!access_ok(buf, len))
		return -EFAULT;

	char kbuf[WRITE_BUF_SIZE];
	size_t done = 0;

	while (done < len) {
		size_t chunk = len - done;
		if (chunk > WRITE_BUF_SIZE)
			chunk = WRITE_BUF_SIZE;

		ssize_t ret = vfs_read(file, kbuf, chunk);
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;

		if (copy_to_user(buf + done, kbuf, (size_t)ret) != 0)
			return -EFAULT;

		done += (size_t)ret;
		if ((size_t)ret < chunk)
			break;
	}

	return (ssize_t)done;
}

ssize_t sys_close(struct trap_frame *tf)
{
	return fd_close((int)tf->a0);
}

ssize_t sys_lseek(struct trap_frame *tf)
{
	struct file *file = fd_get((int)tf->a0);
	loff_t offset = (loff_t)tf->a1;
	int whence = (int)tf->a2;

	if (!file)
		return -EBADF;

	return vfs_llseek(file, offset, whence);
}

ssize_t sys_ioctl(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	uint64_t cmd = tf->a1;

	if (!fd_get(fd))
		return -EBADF;
	if (cmd == TCGETS)
		return 0;

	return 0;
}

ssize_t sys_mkdirat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	uint32_t mode = (uint32_t)tf->a2;
	char path[PATH_BUF_SIZE];
	int ret;

	if (dfd != AT_FDCWD)
		return -EINVAL;

	ret = copy_user_path(path, upath);
	if (ret < 0)
		return ret;

	return vfs_mkdir(path, mode);
}

ssize_t sys_unlinkat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	int flags = (int)tf->a2;
	char path[PATH_BUF_SIZE];
	int ret;

	if (dfd != AT_FDCWD)
		return -EINVAL;
	if (flags & ~AT_REMOVEDIR)
		return -EINVAL;

	ret = copy_user_path(path, upath);
	if (ret < 0)
		return ret;

	return vfs_unlink(path, flags);
}

ssize_t sys_chdir(struct trap_frame *tf)
{
	const char *upath = (const char *)tf->a0;
	char path[PATH_BUF_SIZE];
	struct dentry *dentry;
	int ret;

	ret = copy_user_path(path, upath);
	if (ret < 0)
		return ret;

	dentry = path_lookup(path, 0);
	if (!dentry)
		return -ENOENT;
	if (!dentry->d_inode || (dentry->d_inode->i_mode & S_IFMT) != S_IFDIR) {
		dput(dentry);
		return -ENOTDIR;
	}

	if (current->cwd)
		dput(current->cwd);
	current->cwd = dentry;
	return 0;
}

ssize_t sys_getcwd(struct trap_frame *tf)
{
	char *ubuf = (char *)tf->a0;
	size_t size = tf->a1;
	char path[PATH_BUF_SIZE];
	int ret;

	if (!ubuf || size == 0)
		return -EINVAL;

	ret = getcwd_build(path, sizeof(path));
	if (ret < 0)
		return ret;
	if ((size_t)ret > size)
		return -ERANGE;
	if (!access_ok(ubuf, (size_t)ret))
		return -EFAULT;
	if (copy_to_user(ubuf, path, (size_t)ret) != 0)
		return -EFAULT;

	return (ssize_t)(uintptr_t)ubuf;
}

ssize_t sys_getdents64(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	char *dirp = (char *)tf->a1;
	size_t count = tf->a2;
	struct file *file = fd_get(fd);
	char kbuf[WRITE_BUF_SIZE];
	struct getdents_ctx ctx;
	loff_t start;
	int ret;

	if (!file)
		return -EBADF;
	if (!access_ok(dirp, count))
		return -EFAULT;

	start = file->f_pos;
	memset(&ctx, 0, sizeof(ctx));
	ctx.dirp = kbuf;
	ctx.count = sizeof(kbuf);

	while (ctx.written < count) {
		size_t chunk = count - ctx.written;
		if (chunk > sizeof(kbuf))
			chunk = sizeof(kbuf);

		memset(kbuf, 0, sizeof(kbuf));
		ctx.dirp = kbuf;
		ctx.count = chunk;
		ctx.written = 0;
		ctx.pos = file->f_pos;

		ret = vfs_readdir(file, &ctx, filldir64);
		if (ret < 0) {
			file->f_pos = start;
			return ret;
		}
		if (ctx.written == 0)
			break;

		if (copy_to_user(dirp, kbuf, ctx.written) != 0) {
			file->f_pos = start;
			return -EFAULT;
		}

		dirp += ctx.written;
		start = file->f_pos;
		count -= ctx.written;
		if (ret == 0)
			break;
	}

	return (ssize_t)((uintptr_t)dirp - tf->a1);
}

ssize_t sys_fstat(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	struct kstat *ustat = (struct kstat *)tf->a1;
	struct file *file = fd_get(fd);
	struct kstat st;

	if (!file)
		return -EBADF;
	if (!access_ok(ustat, sizeof(*ustat)))
		return -EFAULT;

	fill_kstat(&st, file->f_inode);
	if (copy_to_user(ustat, &st, sizeof(st)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_mknod(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	uint32_t mode = (uint32_t)tf->a2;
	dev_t dev = (dev_t)tf->a3;
	char path[PATH_BUF_SIZE];
	int ret;

	if (dfd != AT_FDCWD)
		return -EINVAL;

	ret = copy_user_path(path, upath);
	if (ret < 0)
		return ret;

	return vfs_mknod(path, mode, dev);
}

ssize_t sys_dup(struct trap_frame *tf)
{
	return fd_dup((int)tf->a0);
}

ssize_t sys_dup3(struct trap_frame *tf)
{
	int oldfd = (int)tf->a0;
	int newfd = (int)tf->a1;
	int flags = (int)tf->a2;

	if (flags != 0)
		return -EINVAL;

	return fd_dup2(oldfd, newfd);
}

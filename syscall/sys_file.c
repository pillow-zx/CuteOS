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
#include <kernel/buddy.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/vfs.h>
#include <asm/page.h>
#include <asm/trap.h>

/* 内核临时缓冲区大小，sys_write 分块拷贝使用 */
#define WRITE_BUF_SIZE 256

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define TCGETS 0x5401

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

#define AT_EMPTY_PATH	    0x1000
#define AT_SYMLINK_NOFOLLOW 0x100

#define FALLOC_FL_KEEP_SIZE 0x01

static_assert(VFS_PATH_MAX <= PAGE_SIZE,
	      "syscall path buffers are allocated as one page");

/*
 * ftruncate/fallocate 允许设置的最大 i_size。真实上界取决于具体文件系统；
 * 此处取保守值，主要防止无界膨胀导致 fill_kstat 的 st_blocks 计算溢出，
 * 以及未来 extent/块分配逻辑对越界 i_size 误判。
 */
#define MAX_FILE_SIZE (1ULL << 40) /* 1 TiB */

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

struct sys_iovec {
	uint64_t iov_base;
	uint64_t iov_len;
};

static struct file *fd_get_readable(int fd)
{
	struct file *file = fd_get(fd);

	if (!file || !(file->f_mode & FMODE_READ) || !file->f_op ||
	    !file->f_op->read)
		return NULL;

	return file;
}

static struct file *fd_get_writable(int fd)
{
	struct file *file = fd_get(fd);

	if (!file || !(file->f_mode & FMODE_WRITE) || !file->f_op ||
	    !file->f_op->write)
		return NULL;

	return file;
}

static uint32_t apply_umask(uint32_t mode)
{
	if (!current)
		return mode;

	return mode & ~current->umask;
}

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

static int copy_user_path(char **pathp, const char *user)
{
	char *dst;
	ssize_t len;

	if (pathp)
		*pathp = NULL;
	if (!pathp)
		return -EINVAL;
	if (!user)
		return -EFAULT;

	dst = get_free_page(0);
	if (!dst)
		return -ENOMEM;

	len = strncpy_from_user(dst, user, VFS_PATH_MAX);
	if (len < 0) {
		free_page(dst, 0);
		return (int)len;
	}
	if (len == 0) {
		free_page(dst, 0);
		return -ENOENT;
	}

	*pathp = dst;
	return 0;
}

static ssize_t rw_user_buffer(struct file *file, void *buf, size_t len,
			      bool write)
{
	char kbuf[WRITE_BUF_SIZE];
	size_t done = 0;

	if (!access_ok(buf, len))
		return -EFAULT;

	while (done < len) {
		size_t chunk = len - done;
		ssize_t ret;

		if (chunk > WRITE_BUF_SIZE)
			chunk = WRITE_BUF_SIZE;

		if (write) {
			if (copy_from_user(kbuf, (char *)buf + done, chunk) !=
			    0)
				return done ? (ssize_t)done : -EFAULT;
			ret = vfs_write(file, kbuf, chunk);
		} else {
			ret = vfs_read(file, kbuf, chunk);
			if (ret > 0) {
				size_t left = copy_to_user((char *)buf + done,
							   kbuf, (size_t)ret);

				if (left != 0) {
					/*
					 * vfs_read 已推进 f_pos 整个 ret，但只
					 * 有 (ret - left)
					 * 字节真正送达用户。回退
					 * 未送达的尾部，避免下次读静默跳过这些
					 * 字节。
					 */
					file->f_pos -= (loff_t)left;
					done += (size_t)ret - left;
					return done ? (ssize_t)done : -EFAULT;
				}
			}
		}

		if (ret < 0)
			return ret;
		if (ret == 0)
			break;

		done += (size_t)ret;
		if ((size_t)ret < chunk)
			break;
	}

	return (ssize_t)done;
}

static ssize_t rw_at_offset(struct file *file, void *buf, size_t len,
			    loff_t offset, bool write)
{
	loff_t old_pos;
	ssize_t ret;

	if (offset < 0)
		return -EINVAL;

	old_pos = file->f_pos;
	file->f_pos = offset;
	ret = rw_user_buffer(file, buf, len, write);
	file->f_pos = old_pos;

	return ret;
}

static ssize_t rw_iovec(struct file *file, const struct sys_iovec *uiov,
			size_t iovcnt, bool write)
{
	struct sys_iovec iov;
	ssize_t total = 0;

	if (iovcnt > 64)
		return -EINVAL;

	for (size_t i = 0; i < iovcnt; i++) {
		size_t done = 0;

		if (copy_from_user(&iov, uiov + i, sizeof(iov)) != 0)
			return total ? total : -EFAULT;
		if (!access_ok((void *)(uintptr_t)iov.iov_base, iov.iov_len))
			return total ? total : -EFAULT;

		while (done < iov.iov_len) {
			ssize_t ret = rw_user_buffer(
				file, (void *)(uintptr_t)(iov.iov_base + done),
				(size_t)(iov.iov_len - done), write);

			if (ret < 0)
				return total ? total : ret;
			if (ret == 0)
				return total;

			done += (size_t)ret;
			total += ret;
		}
	}

	return total;
}

static int filldir64(void *arg, const char *name, size_t namelen, uint64_t ino,
		     uint8_t type)
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
	char *path;
	int ret;

	if (dfd != AT_FDCWD)
		return -EINVAL;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = vfs_open(path, flags, apply_umask(mode));
	free_page(path, 0);
	return ret;
}

ssize_t sys_write(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const char *buf = (const char *)tf->a1;
	size_t len = tf->a2;
	struct file *file = fd_get_writable(fd);

	if (!file)
		return -EBADF;

	return rw_user_buffer(file, (void *)buf, len, true);
}

ssize_t sys_read(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	char *buf = (char *)tf->a1;
	size_t len = tf->a2;
	struct file *file = fd_get_readable(fd);

	if (!file)
		return -EBADF;

	return rw_user_buffer(file, buf, len, false);
}

ssize_t sys_readv(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const struct sys_iovec *uiov = (const struct sys_iovec *)tf->a1;
	size_t iovcnt = tf->a2;
	struct file *file = fd_get_readable(fd);

	if (!file)
		return -EBADF;
	if (!access_ok(uiov, iovcnt * sizeof(*uiov)))
		return -EFAULT;

	return rw_iovec(file, uiov, iovcnt, false);
}

ssize_t sys_writev(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const struct sys_iovec *uiov = (const struct sys_iovec *)tf->a1;
	size_t iovcnt = tf->a2;
	struct file *file = fd_get_writable(fd);

	if (!file)
		return -EBADF;
	if (!access_ok(uiov, iovcnt * sizeof(*uiov)))
		return -EFAULT;

	return rw_iovec(file, uiov, iovcnt, true);
}

ssize_t sys_pread64(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	char *buf = (char *)tf->a1;
	size_t len = tf->a2;
	loff_t offset = (loff_t)tf->a3;
	struct file *file = fd_get_readable(fd);

	if (!file)
		return -EBADF;

	return rw_at_offset(file, buf, len, offset, false);
}

ssize_t sys_pwrite64(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const char *buf = (const char *)tf->a1;
	size_t len = tf->a2;
	loff_t offset = (loff_t)tf->a3;
	struct file *file = fd_get_writable(fd);

	if (!file)
		return -EBADF;

	return rw_at_offset(file, (void *)buf, len, offset, true);
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
	char *path;
	int ret;

	if (dfd != AT_FDCWD)
		return -EINVAL;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = vfs_mkdir(path, apply_umask(mode));
	free_page(path, 0);
	return ret;
}

ssize_t sys_unlinkat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	int flags = (int)tf->a2;
	char *path;
	int ret;

	if (dfd != AT_FDCWD)
		return -EINVAL;
	if (flags & ~AT_REMOVEDIR)
		return -EINVAL;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = vfs_unlink(path, flags);
	free_page(path, 0);
	return ret;
}

ssize_t sys_chdir(struct trap_frame *tf)
{
	const char *upath = (const char *)tf->a0;
	char *path;
	struct dentry *dentry;
	int ret;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = path_lookup_err(path, 0, &dentry);
	free_page(path, 0);
	if (ret < 0)
		return ret;
	return vfs_chdir_dentry(dentry);
}

ssize_t sys_faccessat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	int mode = (int)tf->a2;
	char *path;
	struct dentry *dentry;
	int ret;

	if (dfd != AT_FDCWD)
		return -EINVAL;
	if (mode & ~(R_OK | W_OK | X_OK))
		return -EINVAL;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = path_lookup_err(path, 0, &dentry);
	free_page(path, 0);
	if (ret < 0)
		return ret;
	ret = vfs_inode_permission(dentry->d_inode, (uint32_t)mode);
	dput(dentry);

	return ret;
}

ssize_t sys_getcwd(struct trap_frame *tf)
{
	char *ubuf = (char *)tf->a0;
	size_t size = tf->a1;
	char *path;
	int ret;

	if (!ubuf || size == 0)
		return -EINVAL;

	path = get_free_page(0);
	if (!path)
		return -ENOMEM;

	ret = vfs_getcwd_path(current ? current->cwd : NULL, path,
			      VFS_PATH_MAX);
	if (ret < 0)
		goto out;
	if ((size_t)ret > size) {
		ret = -ERANGE;
		goto out;
	}
	if (!access_ok(ubuf, (size_t)ret)) {
		ret = -EFAULT;
		goto out;
	}
	if (copy_to_user(ubuf, path, (size_t)ret) != 0) {
		ret = -EFAULT;
		goto out;
	}
	ret = (ssize_t)(uintptr_t)ubuf;

out:
	free_page(path, 0);
	return ret;
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

	vfs_stat_file(file, &st);
	if (copy_to_user(ustat, &st, sizeof(st)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_newfstatat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	struct kstat *ustat = (struct kstat *)tf->a2;
	int flags = (int)tf->a3;
	char *path;
	struct dentry *dentry;
	struct kstat st;
	int ret;

	if (!ustat || !access_ok(ustat, sizeof(*ustat)))
		return -EFAULT;
	if (flags & ~(AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW))
		return -EINVAL;

	if ((flags & AT_EMPTY_PATH) && upath) {
		char first;

		if (copy_from_user(&first, upath, sizeof(first)) != 0)
			return -EFAULT;

		if (first == '\0') {
			struct file *file = fd_get(dfd);

			if (!file)
				return -EBADF;
			vfs_stat_file(file, &st);
			if (copy_to_user(ustat, &st, sizeof(st)) != 0)
				return -EFAULT;
			return 0;
		}
	}

	if (dfd != AT_FDCWD)
		return -EINVAL;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = path_lookup_err(
		path, (flags & AT_SYMLINK_NOFOLLOW) ? LOOKUP_NOFOLLOW : 0,
		&dentry);
	free_page(path, 0);
	if (ret < 0)
		return ret;

	vfs_stat_dentry(dentry, &st);
	dput(dentry);
	if (copy_to_user(ustat, &st, sizeof(st)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_readlinkat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	char *ubuf = (char *)tf->a2;
	size_t bufsiz = (size_t)tf->a3;
	char *path;
	char *link;
	size_t link_size;
	struct dentry *dentry;
	int len;
	int ret;

	if (dfd != AT_FDCWD)
		return -EINVAL;
	if (bufsiz == 0)
		return -EINVAL;
	if (!ubuf || !access_ok(ubuf, bufsiz))
		return -EFAULT;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	/* readlink 操作链接本身，绝不跟随末端符号链接。 */
	ret = path_lookup_err(path, LOOKUP_NOFOLLOW, &dentry);
	free_page(path, 0);
	if (ret < 0)
		return ret;

	link_size = bufsiz < VFS_PATH_MAX ? bufsiz : VFS_PATH_MAX;
	link = get_free_page(0);
	if (!link) {
		dput(dentry);
		return -ENOMEM;
	}

	len = vfs_readlink(dentry, link, link_size);
	dput(dentry);
	if (len < 0) {
		free_page(link, 0);
		return len;
	}

	if ((size_t)len > bufsiz)
		len = (int)bufsiz;
	if (copy_to_user(ubuf, link, (size_t)len) != 0) {
		free_page(link, 0);
		return -EFAULT;
	}

	free_page(link, 0);
	return len;
}

ssize_t sys_mknod(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	uint32_t mode = (uint32_t)tf->a2;
	dev_t dev = (dev_t)tf->a3;
	char *path;
	int ret;

	if (dfd != AT_FDCWD)
		return -EINVAL;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = vfs_mknod(path, apply_umask(mode), dev);
	free_page(path, 0);
	return ret;
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

ssize_t sys_fsync(struct trap_frame *tf)
{
	struct file *file = fd_get((int)tf->a0);

	if (!file)
		return -EBADF;

	return vfs_sync_file(file);
}

ssize_t sys_fdatasync(struct trap_frame *tf)
{
	return sys_fsync(tf);
}

ssize_t sys_ftruncate64(struct trap_frame *tf)
{
	struct file *file = fd_get((int)tf->a0);
	int64_t length = (int64_t)tf->a1;

	if (!file || !(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (length < 0 || length > MAX_FILE_SIZE)
		return -EINVAL;
	if (!file->f_inode)
		return -EINVAL;

	return vfs_truncate_file(file, (uint64_t)length);
}

ssize_t sys_fallocate(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(ext2): 需要真正分配/预留数据块后才能实现 fallocate 语义。 */
	return -ENOSYS;
}

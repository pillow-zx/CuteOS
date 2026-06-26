/*
 * syscall/sys_file_path.c - 路径与目录操作系统调用
 *
 * 覆盖范围：
 *   用户空间路径拷贝（copy_user_path）、dirfd 解析（dirfd_path_base）、
 *   umask 应用、目录条目格式转换，以及所有基于路径的文件系统操作。
 */

#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/fs_struct.h>
#include <kernel/signal.h>
#include <kernel/stat.h>
#include <kernel/statfs.h>
#include <kernel/types.h>
#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <kernel/buddy.h>
#include <kernel/pipe.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/vfs.h>
#include <asm/page.h>
#include <asm/trap.h>
#include <kernel/time.h>

/* 内核临时缓冲区大小，sys_getdents64 分块拷贝使用 */
#define WRITE_BUF_SIZE 256

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

static_assert(VFS_PATH_MAX <= PAGE_SIZE,
	      "syscall path buffers are allocated as one page");

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

int copy_user_path(char **pathp, const char *user)
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

int dirfd_path_base(int dfd, const char *path, struct dentry **basep)
{
	struct file *file;
	int ret = 0;

	if (basep)
		*basep = NULL;
	if (!basep)
		return -EINVAL;
	if (!path)
		return -EFAULT;
	if (path[0] == '/' || dfd == AT_FDCWD)
		return 0;

	file = fd_get(dfd);
	if (!file)
		return -EBADF;
	if (!file->f_dentry || !file->f_inode ||
	    !S_ISDIR(file->f_inode->i_mode)) {
		ret = -ENOTDIR;
		goto out;
	}

	dget(file->f_dentry);
	*basep = file->f_dentry;

out:
	file_put(file);
	return ret;
}

static uint32_t apply_umask(uint32_t mode)
{
	if (!current)
		return mode;

	return mode & ~fs_get_umask(current->fs);
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
	struct dentry *base;
	int ret;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base(dfd, path, &base);
	if (ret < 0)
		goto out;

	ret = vfs_openat(base, path, flags, apply_umask(mode));
	if (base)
		dput(base);
out:
	free_page(path, 0);
	return ret;
}

ssize_t sys_mkdirat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	uint32_t mode = (uint32_t)tf->a2;
	char *path;
	struct dentry *base;
	int ret;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base(dfd, path, &base);
	if (ret < 0)
		goto out;

	ret = vfs_mkdir_at(base, path, apply_umask(mode));
	if (base)
		dput(base);
out:
	free_page(path, 0);
	return ret;
}

ssize_t sys_unlinkat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	int flags = (int)tf->a2;
	char *path;
	struct dentry *base;
	int ret;

	if (flags & ~AT_REMOVEDIR)
		return -EINVAL;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base(dfd, path, &base);
	if (ret < 0)
		goto out;

	ret = vfs_unlink_at(base, path, flags);
	if (base)
		dput(base);
out:
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
	struct dentry *base;
	struct dentry *dentry;
	int ret;

	if (mode & ~(R_OK | W_OK | X_OK))
		return -EINVAL;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base(dfd, path, &base);
	if (ret < 0)
		goto out;

	ret = path_lookupat_err(base, path, 0, &dentry);
	if (base)
		dput(base);
out:
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
	struct dentry *cwd;
	char *path;
	int ret;

	if (!ubuf || size == 0)
		return -EINVAL;

	path = get_free_page(0);
	if (!path)
		return -ENOMEM;

	cwd = fs_get_cwd_dentry(current ? current->fs : NULL);
	ret = vfs_getcwd_path(cwd, path, VFS_PATH_MAX);
	if (cwd)
		dput(cwd);
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
	ssize_t result;

	if (!file)
		return -EBADF;
	if (!access_ok(dirp, count)) {
		file_put(file);
		return -EFAULT;
	}

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
			result = ret;
			goto out_put;
		}
		if (ctx.written == 0)
			break;

		if (copy_to_user(dirp, kbuf, ctx.written) != 0) {
			file->f_pos = start;
			result = -EFAULT;
			goto out_put;
		}

		dirp += ctx.written;
		start = file->f_pos;
		count -= ctx.written;
	}

	result = (ssize_t)((uintptr_t)dirp - tf->a1);
out_put:
	file_put(file);
	return result;
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
	struct dentry *base;
	struct dentry *dentry;
	int len;
	int ret;

	if (bufsiz == 0)
		return -EINVAL;
	if (!ubuf || !access_ok(ubuf, bufsiz))
		return -EFAULT;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	/* readlink 操作链接本身，绝不跟随末端符号链接。 */
	ret = dirfd_path_base(dfd, path, &base);
	if (ret < 0)
		goto out_free_path;

	ret = path_lookupat_err(base, path, LOOKUP_NOFOLLOW, &dentry);
	if (base)
		dput(base);
out_free_path:
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
	struct dentry *base;
	int ret;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base(dfd, path, &base);
	if (ret < 0)
		goto out;

	ret = vfs_mknod_at(base, path, apply_umask(mode), dev);
	if (base)
		dput(base);
out:
	free_page(path, 0);
	return ret;
}

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

#include "sys_file_internal.h"

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

#define AT_EMPTY_PATH	    0x1000
#define AT_EACCESS	    0x200
#define AT_SYMLINK_NOFOLLOW 0x100

struct getdents_ctx {
	char *dirp;
	size_t count;
	size_t written;
};

static uint32_t apply_umask(uint32_t mode)
{
	if (!current)
		return mode;

	return mode & ~fs_get_umask(task_fs(current));
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

static int sys_faccessat_path(int dfd, const char *upath, int mode,
			      uint32_t lookup_flags)
{
	char *path;
	struct dentry *base;
	struct dentry *dentry;
	int ret;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base(dfd, path, &base);
	if (ret < 0)
		goto out;

	ret = path_lookupat_err(base, path, lookup_flags, &dentry);
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

static int sys_faccessat_empty_path(int dfd, int mode)
{
	struct dentry *cwd;
	struct file *file;
	int ret;

	if (dfd == AT_FDCWD) {
		cwd = fs_get_cwd_dentry(task_fs(current));
		if (!cwd)
			return -ENOENT;
		ret = vfs_inode_permission(cwd->d_inode, (uint32_t)mode);
		dput(cwd);
		return ret;
	}

	file = fd_get(dfd);
	if (!file)
		return -EBADF;
	ret = vfs_inode_permission(file->f_inode, (uint32_t)mode);
	file_put(file);
	return ret;
}

static int filldir64(void *arg, const char *name, size_t namelen, uint64_t ino,
		     uint8_t type, loff_t off)
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
	dirent->d_off = off;
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

	if (mode & ~(R_OK | W_OK | X_OK))
		return -EINVAL;

	return sys_faccessat_path(dfd, upath, mode, 0);
}

ssize_t sys_faccessat2(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	int mode = (int)tf->a2;
	int flags = (int)tf->a3;

	if (mode & ~(R_OK | W_OK | X_OK))
		return -EINVAL;
	if (flags & ~(AT_EACCESS | AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW))
		return -EINVAL;
	if (flags & AT_EMPTY_PATH) {
		char first;

		if (!upath)
			return -EFAULT;
		if (copy_from_user(&first, upath, sizeof(first)) != 0)
			return -EFAULT;
		if (first == '\0')
			return sys_faccessat_empty_path(dfd, mode);
	}

	return sys_faccessat_path(
		dfd, upath, mode,
		(flags & AT_SYMLINK_NOFOLLOW) ? LOOKUP_NOFOLLOW : 0);
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

	cwd = fs_get_cwd_dentry(task_fs(current));
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
	char kbuf[SYS_FILE_BUF_SIZE];
	struct getdents_ctx ctx;
	loff_t start;
	int ret;
	ssize_t result;

	if (!file)
		return -EBADF;
	if (count == 0) {
		file_put(file);
		return -EINVAL;
	}
	if (!access_ok(dirp, count)) {
		file_put(file);
		return -EFAULT;
	}

	start = file->f_pos;
	memset(&ctx, 0, sizeof(ctx));
	ctx.dirp = kbuf;
	ctx.count = sizeof(kbuf);

	while ((size_t)((uintptr_t)dirp - tf->a1) < count) {
		size_t done = (size_t)((uintptr_t)dirp - tf->a1);
		size_t chunk = count - done;
		if (chunk > sizeof(kbuf))
			chunk = sizeof(kbuf);

		memset(kbuf, 0, sizeof(kbuf));
		ctx.dirp = kbuf;
		ctx.count = chunk;
		ctx.written = 0;

		ret = vfs_readdir(file, &ctx, filldir64);
		if (ret < 0) {
			if (ctx.written == 0) {
				file->f_pos = start;
				result = done ? (ssize_t)done : ret;
				goto out_put;
			}
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
		if (ret < 0)
			break;
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

ssize_t sys_renameat2(struct trap_frame *tf)
{
	int old_dfd = (int)tf->a0;
	const char *uold_path = (const char *)tf->a1;
	int new_dfd = (int)tf->a2;
	const char *unew_path = (const char *)tf->a3;
	unsigned int flags = (unsigned int)tf->a4;
	char *old_path = NULL;
	char *new_path = NULL;
	struct dentry *old_base, *new_base;
	int ret;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	ret = copy_user_path(&old_path, uold_path);
	if (ret < 0)
		return ret;

	ret = copy_user_path(&new_path, unew_path);
	if (ret < 0) {
		free_page(old_path, 0);
		return ret;
	}

	ret = dirfd_path_base(old_dfd, old_path, &old_base);
	if (ret < 0)
		goto out;

	ret = dirfd_path_base(new_dfd, new_path, &new_base);
	if (ret < 0) {
		if (old_base)
			dput(old_base);
		goto out;
	}

	ret = vfs_rename_at(old_base, old_path, new_base, new_path, flags);
	if (old_base)
		dput(old_base);
	if (new_base)
		dput(new_base);
out:
	free_page(old_path, 0);
	free_page(new_path, 0);
	return ret;
}

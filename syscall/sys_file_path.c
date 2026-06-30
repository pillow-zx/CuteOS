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
#define AT_SYMLINK_FOLLOW   0x400

#define UTIME_NOW  0x3fffffff
#define UTIME_OMIT 0x3ffffffe

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
	struct path base;
	struct path found;
	int ret;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base_path(dfd, path, &base);
	if (ret < 0)
		goto out;

	ret = path_lookupat_path(base.dentry ? &base : NULL, path,
				 lookup_flags, &found);
	if (base.dentry)
		path_put(&base);
out:
	free_page(path, 0);
	if (ret < 0)
		return ret;
	ret = vfs_inode_permission(found.dentry->d_inode, (uint32_t)mode);
	path_put(&found);

	return ret;
}

static int sys_faccessat_empty_path(int dfd, int mode)
{
	struct path cwd;
	struct file *file;
	int ret;

	if (dfd == AT_FDCWD) {
		ret = fs_get_cwd_path(task_fs(current), &cwd);
		if (ret < 0)
			return ret;
		ret = vfs_inode_permission(cwd.dentry->d_inode,
					   (uint32_t)mode);
		path_put(&cwd);
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
	struct path base;
	int ret;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base_path(dfd, path, &base);
	if (ret < 0)
		goto out;

	ret = vfs_openat_path(base.dentry ? &base : NULL, path, flags,
			      apply_umask(mode));
	if (base.dentry)
		path_put(&base);
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
	struct path base;
	int ret;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base_path(dfd, path, &base);
	if (ret < 0)
		goto out;

	ret = vfs_mkdir_at_path(base.dentry ? &base : NULL, path,
				apply_umask(mode));
	if (base.dentry)
		path_put(&base);
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
	struct path base;
	int ret;

	if (flags & ~AT_REMOVEDIR)
		return -EINVAL;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base_path(dfd, path, &base);
	if (ret < 0)
		goto out;

	ret = vfs_unlink_at_path(base.dentry ? &base : NULL, path, flags);
	if (base.dentry)
		path_put(&base);
out:
	free_page(path, 0);
	return ret;
}

ssize_t sys_chdir(struct trap_frame *tf)
{
	const char *upath = (const char *)tf->a0;
	char *path;
	struct path found;
	int ret;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = path_lookupat_path(NULL, path, 0, &found);
	free_page(path, 0);
	if (ret < 0)
		return ret;

	ret = vfs_chdir_path(&found);
	path_put(&found);
	return ret;
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
	struct path cwd;
	char *path;
	int ret;

	if (!ubuf || size == 0)
		return -EINVAL;

	path = get_free_page(0);
	if (!path)
		return -ENOMEM;

	ret = fs_get_cwd_path(task_fs(current), &cwd);
	if (ret < 0)
		goto out;
	ret = vfs_getcwd_path(&cwd, path, VFS_PATH_MAX);
	path_put(&cwd);
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
	struct path base;
	struct path found;
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
	ret = dirfd_path_base_path(dfd, path, &base);
	if (ret < 0)
		goto out_free_path;

	ret = path_lookupat_path(base.dentry ? &base : NULL, path,
				 LOOKUP_NOFOLLOW, &found);
	if (base.dentry)
		path_put(&base);
out_free_path:
	free_page(path, 0);
	if (ret < 0)
		return ret;

	link_size = bufsiz < VFS_PATH_MAX ? bufsiz : VFS_PATH_MAX;
	link = get_free_page(0);
	if (!link) {
		path_put(&found);
		return -ENOMEM;
	}

	len = vfs_readlink(found.dentry, link, link_size);
	path_put(&found);
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

ssize_t sys_symlinkat(struct trap_frame *tf)
{
	const char *utarget = (const char *)tf->a0;
	int newdfd = (int)tf->a1;
	const char *ulinkpath = (const char *)tf->a2;
	char *target;
	char *linkpath;
	struct path base;
	int ret;

	ret = copy_user_path(&target, utarget);
	if (ret < 0)
		return ret;
	ret = copy_user_path(&linkpath, ulinkpath);
	if (ret < 0) {
		free_page(target, 0);
		return ret;
	}

	ret = dirfd_path_base_path(newdfd, linkpath, &base);
	if (ret < 0)
		goto out;

	ret = vfs_symlink_at_path(base.dentry ? &base : NULL, target,
				  linkpath);
	if (base.dentry)
		path_put(&base);
out:
	free_page(linkpath, 0);
	free_page(target, 0);
	return ret;
}

ssize_t sys_linkat(struct trap_frame *tf)
{
	int olddfd = (int)tf->a0;
	const char *uoldpath = (const char *)tf->a1;
	int newdfd = (int)tf->a2;
	const char *unewpath = (const char *)tf->a3;
	int flags = (int)tf->a4;
	char *oldpath;
	char *newpath;
	struct path old_base;
	struct path new_base;
	struct path old_path_found;
	int ret;

	if (flags & ~AT_SYMLINK_FOLLOW)
		return -EINVAL;

	ret = copy_user_path(&oldpath, uoldpath);
	if (ret < 0)
		return ret;
	ret = copy_user_path(&newpath, unewpath);
	if (ret < 0) {
		free_page(oldpath, 0);
		return ret;
	}

	ret = dirfd_path_base_path(olddfd, oldpath, &old_base);
	if (ret < 0)
		goto out_free_paths;
	ret = path_lookupat_path(
		old_base.dentry ? &old_base : NULL, oldpath,
		(flags & AT_SYMLINK_FOLLOW) ? 0 : LOOKUP_NOFOLLOW,
		&old_path_found);
	if (old_base.dentry)
		path_put(&old_base);
	if (ret < 0)
		goto out_free_paths;

	ret = dirfd_path_base_path(newdfd, newpath, &new_base);
	if (ret < 0) {
		path_put(&old_path_found);
		goto out_free_paths;
	}
	ret = vfs_link_at_path(old_path_found.dentry,
			       new_base.dentry ? &new_base : NULL, newpath);
	if (new_base.dentry)
		path_put(&new_base);
	path_put(&old_path_found);

out_free_paths:
	free_page(newpath, 0);
	free_page(oldpath, 0);
	return ret;
}

ssize_t sys_mknod(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	uint32_t mode = (uint32_t)tf->a2;
	dev_t dev = (dev_t)tf->a3;
	char *path;
	struct path base;
	int ret;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base_path(dfd, path, &base);
	if (ret < 0)
		goto out;

	ret = vfs_mknod_at_path(base.dentry ? &base : NULL, path,
				apply_umask(mode), dev);
	if (base.dentry)
		path_put(&base);
out:
	free_page(path, 0);
	return ret;
}

ssize_t sys_umount2(struct trap_frame *tf)
{
	const char *utarget = (const char *)tf->a0;
	int flags = (int)tf->a1;
	char *target;
	int ret;

	ret = copy_user_path(&target, utarget);
	if (ret < 0)
		return ret;

	ret = vfs_umount(target, flags);
	free_page(target, 0);
	return ret;
}

ssize_t sys_mount(struct trap_frame *tf)
{
	const char *usource = (const char *)tf->a0;
	const char *utarget = (const char *)tf->a1;
	const char *utype = (const char *)tf->a2;
	unsigned long flags = (unsigned long)tf->a3;
	const void *data = (const void *)tf->a4;
	char *source = NULL;
	char *target = NULL;
	char *type = NULL;
	int ret;

	ret = copy_user_path(&source, usource);
	if (ret < 0)
		goto out;
	ret = copy_user_path(&target, utarget);
	if (ret < 0)
		goto out;
	ret = copy_user_path(&type, utype);
	if (ret < 0)
		goto out;

	ret = vfs_mount(source, target, type, flags, data);

out:
	if (type)
		free_page(type, 0);
	if (target)
		free_page(target, 0);
	if (source)
		free_page(source, 0);
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
	struct path old_base;
	struct path new_base;
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

	ret = dirfd_path_base_path(old_dfd, old_path, &old_base);
	if (ret < 0)
		goto out;

	ret = dirfd_path_base_path(new_dfd, new_path, &new_base);
	if (ret < 0) {
		if (old_base.dentry)
			path_put(&old_base);
		goto out;
	}

	ret = vfs_rename_at_path(old_base.dentry ? &old_base : NULL, old_path,
				 new_base.dentry ? &new_base : NULL, new_path,
				 flags);
	if (old_base.dentry)
		path_put(&old_base);
	if (new_base.dentry)
		path_put(&new_base);
out:
	free_page(old_path, 0);
	free_page(new_path, 0);
	return ret;
}

static int sys_utimensat_empty_path(int dfd, const struct sys_timespec ktimes[2],
				    const bool set_time[2])
{
	struct file *file;
	struct path cwd;
	int ret;

	if (dfd == AT_FDCWD) {
		ret = fs_get_cwd_path(task_fs(current), &cwd);
		if (ret < 0)
			return ret;
		ret = vfs_inode_set_timestamps(cwd.dentry->d_inode,
					       ktimes[0].tv_sec,
					       ktimes[1].tv_sec, set_time[0],
					       set_time[1]);
		path_put(&cwd);
		return ret;
	}

	file = fd_get(dfd);
	if (!file)
		return -EBADF;
	ret = vfs_inode_set_timestamps(file->f_inode, ktimes[0].tv_sec,
				       ktimes[1].tv_sec, set_time[0],
				       set_time[1]);
	file_put(file);
	return ret;
}

static int sys_utimensat_read_times(const struct sys_timespec *utimes,
				    struct sys_timespec ktimes[2],
				    bool set_time[2])
{
	struct sys_timespec now;

	mtime_to_timespec(arch_timer_now(), &now);
	if (!utimes) {
		ktimes[0] = now;
		ktimes[1] = now;
		set_time[0] = true;
		set_time[1] = true;
		return 0;
	}
	if (copy_from_user(ktimes, utimes, sizeof(struct sys_timespec) * 2) != 0)
		return -EFAULT;

	for (int i = 0; i < 2; i++) {
		set_time[i] = true;
		if (ktimes[i].tv_nsec == UTIME_OMIT) {
			set_time[i] = false;
			continue;
		}
		if (ktimes[i].tv_nsec == UTIME_NOW) {
			ktimes[i] = now;
			continue;
		}
		if (ktimes[i].tv_sec < 0 || ktimes[i].tv_sec > UINT32_MAX ||
		    ktimes[i].tv_nsec < 0 || ktimes[i].tv_nsec >= 1000000000LL)
			return -EINVAL;
	}

	return 0;
}

ssize_t sys_utimensat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	const struct sys_timespec *utimes = (const struct sys_timespec *)tf->a2;
	int flags = (int)tf->a3;
	struct sys_timespec ktimes[2];
	bool set_time[2];
	char *path;
	struct path base;
	struct path found;
	int ret;

	if (flags & ~(AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW))
		return -EINVAL;

	ret = sys_utimensat_read_times(utimes, ktimes, set_time);
	if (ret < 0)
		return ret;

	if ((flags & AT_EMPTY_PATH) && !upath)
		return sys_utimensat_empty_path(dfd, ktimes, set_time);
	if ((flags & AT_EMPTY_PATH) && upath) {
		char first;

		if (copy_from_user(&first, upath, sizeof(first)) != 0)
			return -EFAULT;
		if (first == '\0')
			return sys_utimensat_empty_path(dfd, ktimes, set_time);
	}

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base_path(dfd, path, &base);
	if (ret < 0)
		goto out_free_path;

	ret = path_lookupat_path(
		base.dentry ? &base : NULL, path,
		(flags & AT_SYMLINK_NOFOLLOW) ? LOOKUP_NOFOLLOW : 0, &found);
	if (base.dentry)
		path_put(&base);
out_free_path:
	free_page(path, 0);
	if (ret < 0)
		return ret;

	ret = vfs_inode_set_timestamps(found.dentry->d_inode, ktimes[0].tv_sec,
				       ktimes[1].tv_sec, set_time[0],
				       set_time[1]);
	path_put(&found);
	return ret;
}

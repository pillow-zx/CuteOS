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
	char *path __cleanup_with(page0) = NULL;
	struct path base __cleanup_with(path) = {};
	struct path found __cleanup_with(path) = {};
	int ret;

	ret = copy_user_path_at(dfd, upath, &path, &base);
	if (ret < 0)
		return ret;

	ret = path_lookupat_path(base.dentry ? &base : NULL, path,
				 lookup_flags, &found);
	if (ret < 0)
		return ret;

	ret = vfs_inode_permission(found.dentry->d_inode, (uint32_t)mode);

	return ret;
}

static int sys_faccessat_empty_path(int dfd, int mode)
{
	struct path cwd __cleanup_with(path) = {};
	struct file *file __cleanup_with(file) = NULL;
	struct inode *inode;
	int ret;

	ret = sys_empty_path_inode(dfd, &cwd, &file, &inode);
	if (ret < 0)
		return ret;

	ret = vfs_inode_permission(inode, (uint32_t)mode);
	return ret;
}

static int filldir64(void *arg, const char *name, size_t namelen, uint64_t ino,
		     uint8_t type, loff_t off)
{
	struct getdents_ctx *ctx = arg;
	struct linux_dirent64 dirent;
	char *dst;
	size_t name_off = offsetof(struct linux_dirent64, d_name);
	size_t reclen;

	if (namelen > VFS_NAME_MAX)
		return -EINVAL;

	reclen = name_off + namelen + 1;
	reclen = (reclen + 7) & ~7UL;
	if (ctx->written + reclen > ctx->count)
		return -EINVAL;

	dirent.d_ino = ino;
	dirent.d_off = off;
	dirent.d_reclen = (uint16_t)reclen;
	dirent.d_type = vfs_type_to_dirent(type);

	dst = ctx->dirp + ctx->written;
	memset(dst, 0, reclen);
	memcpy(dst, &dirent, name_off);
	memcpy(dst + name_off, name, namelen);

	ctx->written += reclen;
	return 0;
}

ssize_t sys_openat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	uint32_t flags = (uint32_t)tf->a2;
	uint32_t mode = (uint32_t)tf->a3;
	char *path __cleanup_with(page0) = NULL;
	struct path base __cleanup_with(path) = {};
	int ret;

	ret = copy_user_path_at(dfd, upath, &path, &base);
	if (ret < 0)
		return ret;

	ret = vfs_openat_path(base.dentry ? &base : NULL, path, flags,
			      apply_umask(mode));
	return ret;
}

ssize_t sys_mkdirat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	uint32_t mode = (uint32_t)tf->a2;
	char *path __cleanup_with(page0) = NULL;
	struct path base __cleanup_with(path) = {};
	int ret;

	ret = copy_user_path_at(dfd, upath, &path, &base);
	if (ret < 0)
		return ret;

	ret = vfs_mkdir_at_path(base.dentry ? &base : NULL, path,
				apply_umask(mode));
	return ret;
}

ssize_t sys_unlinkat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	int flags = (int)tf->a2;
	char *path __cleanup_with(page0) = NULL;
	struct path base __cleanup_with(path) = {};
	int ret;

	if (flags & ~AT_REMOVEDIR)
		return -EINVAL;

	ret = copy_user_path_at(dfd, upath, &path, &base);
	if (ret < 0)
		return ret;

	ret = vfs_unlink_at_path(base.dentry ? &base : NULL, path, flags);
	return ret;
}

ssize_t sys_chdir(struct trap_frame *tf)
{
	const char *upath = (const char *)tf->a0;
	char *path __cleanup_with(page0) = NULL;
	struct path found __cleanup_with(path) = {};
	int ret;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = path_lookupat_path(NULL, path, 0, &found);
	if (ret < 0)
		return ret;

	ret = vfs_chdir_path(&found);
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
		bool empty;
		int ret;

		if (!upath)
			return -EFAULT;
		ret = sys_empty_path_requested(flags, upath, &empty);
		if (ret < 0)
			return ret;
		if (empty)
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
	struct path cwd __cleanup_with(path) = {};
	char *path __cleanup_with(page0) = NULL;
	int ret;

	if (!ubuf || size == 0)
		return -EINVAL;

	path = get_free_page(0);
	if (!path)
		return -ENOMEM;

	ret = fs_get_cwd_path(task_fs(current), &cwd);
	if (ret < 0)
		return ret;
	ret = vfs_getcwd_path(&cwd, path, VFS_PATH_MAX);
	if (ret < 0)
		return ret;
	if ((size_t)ret > size)
		return -ERANGE;
	if (!access_ok(ubuf, (size_t)ret))
		return -EFAULT;
	if (copy_to_user(ubuf, path, (size_t)ret) != 0)
		return -EFAULT;

	return ret;
}

ssize_t sys_getdents64(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	char *dirp = (char *)tf->a1;
	size_t count = tf->a2;
	struct file *file __cleanup_with(file) = fd_get(fd);
	char kbuf[SYS_FILE_BUF_SIZE];
	struct getdents_ctx ctx;
	loff_t start;
	int ret;
	ssize_t result;

	if (!file)
		return -EBADF;
	if (count == 0)
		return -EINVAL;
	if (!access_ok(dirp, count))
		return -EFAULT;

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
				return done ? (ssize_t)done : ret;
			}
		}
		if (ctx.written == 0)
			break;

		if (copy_to_user(dirp, kbuf, ctx.written) != 0) {
			file->f_pos = start;
			return -EFAULT;
		}

		dirp += ctx.written;
		start = file->f_pos;
		if (ret < 0)
			break;
	}

	result = (ssize_t)((uintptr_t)dirp - tf->a1);
	return result;
}

ssize_t sys_readlinkat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	char *ubuf = (char *)tf->a2;
	size_t bufsiz = (size_t)tf->a3;
	char *path __cleanup_with(page0) = NULL;
	char *link __cleanup_with(page0) = NULL;
	size_t link_size;
	struct path base __cleanup_with(path) = {};
	struct path found __cleanup_with(path) = {};
	int len;
	int ret;

	if (bufsiz == 0)
		return -EINVAL;
	if (!ubuf || !access_ok(ubuf, bufsiz))
		return -EFAULT;

	/* readlink 操作链接本身，绝不跟随末端符号链接。 */
	ret = copy_user_path_at(dfd, upath, &path, &base);
	if (ret < 0)
		return ret;

	ret = path_lookupat_path(base.dentry ? &base : NULL, path,
				 LOOKUP_NOFOLLOW, &found);
	if (ret < 0)
		return ret;

	link_size = bufsiz < VFS_PATH_MAX ? bufsiz : VFS_PATH_MAX;
	link = get_free_page(0);
	if (!link)
		return -ENOMEM;

	len = vfs_readlink(found.dentry, link, link_size);
	if (len < 0)
		return len;

	if ((size_t)len > bufsiz)
		len = (int)bufsiz;
	if (copy_to_user(ubuf, link, (size_t)len) != 0)
		return -EFAULT;

	return len;
}

ssize_t sys_symlinkat(struct trap_frame *tf)
{
	const char *utarget = (const char *)tf->a0;
	int newdfd = (int)tf->a1;
	const char *ulinkpath = (const char *)tf->a2;
	char *target __cleanup_with(page0) = NULL;
	char *linkpath __cleanup_with(page0) = NULL;
	struct path base __cleanup_with(path) = {};
	int ret;

	ret = copy_user_path(&target, utarget);
	if (ret < 0)
		return ret;
	ret = copy_user_path_at(newdfd, ulinkpath, &linkpath, &base);
	if (ret < 0)
		return ret;

	ret = vfs_symlink_at_path(base.dentry ? &base : NULL, target,
				  linkpath);
	return ret;
}

ssize_t sys_linkat(struct trap_frame *tf)
{
	int olddfd = (int)tf->a0;
	const char *uoldpath = (const char *)tf->a1;
	int newdfd = (int)tf->a2;
	const char *unewpath = (const char *)tf->a3;
	int flags = (int)tf->a4;
	char *oldpath __cleanup_with(page0) = NULL;
	char *newpath __cleanup_with(page0) = NULL;
	struct path old_base __cleanup_with(path) = {};
	struct path new_base __cleanup_with(path) = {};
	struct path old_path_found __cleanup_with(path) = {};
	int ret;

	if (flags & ~AT_SYMLINK_FOLLOW)
		return -EINVAL;

	ret = copy_user_path_at(olddfd, uoldpath, &oldpath, &old_base);
	if (ret < 0)
		return ret;
	ret = path_lookupat_path(
		old_base.dentry ? &old_base : NULL, oldpath,
		(flags & AT_SYMLINK_FOLLOW) ? 0 : LOOKUP_NOFOLLOW,
		&old_path_found);
	if (ret < 0)
		return ret;

	ret = copy_user_path_at(newdfd, unewpath, &newpath, &new_base);
	if (ret < 0)
		return ret;
	ret = vfs_link_at_path(old_path_found.dentry,
			       new_base.dentry ? &new_base : NULL, newpath);
	return ret;
}

ssize_t sys_mknod(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	uint32_t mode = (uint32_t)tf->a2;
	dev_t dev = (dev_t)tf->a3;
	char *path __cleanup_with(page0) = NULL;
	struct path base __cleanup_with(path) = {};
	int ret;

	ret = copy_user_path_at(dfd, upath, &path, &base);
	if (ret < 0)
		return ret;

	ret = vfs_mknod_at_path(base.dentry ? &base : NULL, path,
				apply_umask(mode), dev);
	return ret;
}

ssize_t sys_umount2(struct trap_frame *tf)
{
	const char *utarget = (const char *)tf->a0;
	int flags = (int)tf->a1;
	char *target __cleanup_with(page0) = NULL;
	int ret;

	ret = copy_user_path(&target, utarget);
	if (ret < 0)
		return ret;

	ret = vfs_umount(target, flags);
	return ret;
}

ssize_t sys_mount(struct trap_frame *tf)
{
	const char *usource = (const char *)tf->a0;
	const char *utarget = (const char *)tf->a1;
	const char *utype = (const char *)tf->a2;
	unsigned long flags = (unsigned long)tf->a3;
	const void *data = (const void *)tf->a4;
	char *source __cleanup_with(page0) = NULL;
	char *target __cleanup_with(page0) = NULL;
	char *type __cleanup_with(page0) = NULL;
	int ret;

	ret = copy_user_path(&source, usource);
	if (ret < 0)
		return ret;
	ret = copy_user_path(&target, utarget);
	if (ret < 0)
		return ret;
	ret = copy_user_path(&type, utype);
	if (ret < 0)
		return ret;

	ret = vfs_mount(source, target, type, flags, data);
	return ret;
}

ssize_t sys_renameat2(struct trap_frame *tf)
{
	int old_dfd = (int)tf->a0;
	const char *uold_path = (const char *)tf->a1;
	int new_dfd = (int)tf->a2;
	const char *unew_path = (const char *)tf->a3;
	unsigned int flags = (unsigned int)tf->a4;
	char *old_path __cleanup_with(page0) = NULL;
	char *new_path __cleanup_with(page0) = NULL;
	struct path old_base __cleanup_with(path) = {};
	struct path new_base __cleanup_with(path) = {};
	int ret;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	ret = copy_user_path_at(old_dfd, uold_path, &old_path, &old_base);
	if (ret < 0)
		return ret;

	ret = copy_user_path_at(new_dfd, unew_path, &new_path, &new_base);
	if (ret < 0)
		return ret;

	ret = vfs_rename_at_path(old_base.dentry ? &old_base : NULL, old_path,
				 new_base.dentry ? &new_base : NULL, new_path,
				 flags);
	return ret;
}

static int sys_utimensat_empty_path(int dfd, const struct timespec ktimes[2],
				    const bool set_time[2])
{
	struct path cwd __cleanup_with(path) = {};
	struct file *file __cleanup_with(file) = NULL;
	struct inode *inode;
	int ret;

	ret = sys_empty_path_inode(dfd, &cwd, &file, &inode);
	if (ret < 0)
		return ret;
	ret = vfs_inode_set_timestamps(inode, ktimes[0].tv_sec,
				       ktimes[1].tv_sec, set_time[0],
				       set_time[1]);
	return ret;
}

static int sys_utimensat_read_times(const struct timespec *utimes,
				    struct timespec ktimes[2],
				    bool set_time[2])
{
	struct timespec now;

	mtime_to_timespec(arch_timer_now(), &now);
	if (!utimes) {
		ktimes[0] = now;
		ktimes[1] = now;
		set_time[0] = true;
		set_time[1] = true;
		return 0;
	}
	if (copy_from_user(ktimes, utimes, sizeof(struct timespec) * 2) != 0)
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
	const struct timespec *utimes = (const struct timespec *)tf->a2;
	int flags = (int)tf->a3;
	struct timespec ktimes[2];
	bool set_time[2];
	char *path __cleanup_with(page0) = NULL;
	struct path base __cleanup_with(path) = {};
	struct path found __cleanup_with(path) = {};
	int ret;

	if (flags & ~(AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW))
		return -EINVAL;

	ret = sys_utimensat_read_times(utimes, ktimes, set_time);
	if (ret < 0)
		return ret;

	if (flags & AT_EMPTY_PATH) {
		bool empty;

		ret = sys_empty_path_requested(flags, upath, &empty);
		if (ret < 0)
			return ret;
		if (empty)
			return sys_utimensat_empty_path(dfd, ktimes, set_time);
	}

	ret = copy_user_path_at(dfd, upath, &path, &base);
	if (ret < 0)
		return ret;

	ret = path_lookupat_path(
		base.dentry ? &base : NULL, path,
		(flags & AT_SYMLINK_NOFOLLOW) ? LOOKUP_NOFOLLOW : 0, &found);
	if (ret < 0)
		return ret;

	ret = vfs_inode_set_timestamps(found.dentry->d_inode, ktimes[0].tv_sec,
				       ktimes[1].tv_sec, set_time[0],
				       set_time[1]);
	return ret;
}

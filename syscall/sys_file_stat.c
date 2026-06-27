/*
 * syscall/sys_file_stat.c - stat/statfs 元数据查询系统调用
 *
 * 覆盖范围：
 *   fstat、newfstatat（含 AT_EMPTY_PATH 和 AT_SYMLINK_NOFOLLOW）、
 *   statfs64、fstatfs64。
 *
 *   路径解析辅助函数（copy_user_path、dirfd_path_base）来自
 *   sys_file_path.c，通过 sys_file_internal.h 声明引用。
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
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/vfs.h>
#include <asm/page.h>
#include <asm/trap.h>

#include "sys_file_internal.h"

#define AT_EMPTY_PATH	    0x1000
#define AT_SYMLINK_NOFOLLOW 0x100

static int stat_empty_path(int dfd, struct kstat *ustat)
{
	struct kstat st;
	int ret;

	if (dfd == AT_FDCWD) {
		struct dentry *cwd =
			fs_get_cwd_dentry(task_fs(current));

		if (!cwd)
			return -ENOENT;
		vfs_stat_dentry(cwd, &st);
		dput(cwd);
		return copy_to_user(ustat, &st, sizeof(st)) != 0 ? -EFAULT :
								   0;
	} else {
		struct file *file = fd_get(dfd);

		if (!file)
			return -EBADF;
		vfs_stat_file(file, &st);
		ret = copy_to_user(ustat, &st, sizeof(st)) != 0 ? -EFAULT :
								 0;
		file_put(file);
		return ret;
	}
}

ssize_t sys_fstat(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	struct kstat *ustat = (struct kstat *)tf->a1;
	struct file *file = fd_get(fd);
	struct kstat st;
	int ret;

	if (!file)
		return -EBADF;
	if (!access_ok(ustat, sizeof(*ustat))) {
		file_put(file);
		return -EFAULT;
	}

	vfs_stat_file(file, &st);
	ret = copy_to_user(ustat, &st, sizeof(st)) != 0 ? -EFAULT : 0;
	file_put(file);

	return ret;
}

ssize_t sys_newfstatat(struct trap_frame *tf)
{
	int dfd = (int)tf->a0;
	const char *upath = (const char *)tf->a1;
	struct kstat *ustat = (struct kstat *)tf->a2;
	int flags = (int)tf->a3;
	char *path;
	struct dentry *base;
	struct dentry *dentry;
	struct kstat st;
	int ret;

	if (!ustat || !access_ok(ustat, sizeof(*ustat)))
		return -EFAULT;
	if (flags & ~(AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW))
		return -EINVAL;

	if ((flags & AT_EMPTY_PATH) && !upath)
		return stat_empty_path(dfd, ustat);
	if ((flags & AT_EMPTY_PATH) && upath) {
		char first;

		if (copy_from_user(&first, upath, sizeof(first)) != 0)
			return -EFAULT;

		if (first == '\0')
			return stat_empty_path(dfd, ustat);
	}

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base(dfd, path, &base);
	if (ret < 0)
		goto out_free_path;

	ret = path_lookupat_err(
		base, path, (flags & AT_SYMLINK_NOFOLLOW) ? LOOKUP_NOFOLLOW : 0,
		&dentry);
	if (base)
		dput(base);
out_free_path:
	free_page(path, 0);
	if (ret < 0)
		return ret;

	vfs_stat_dentry(dentry, &st);
	dput(dentry);
	if (copy_to_user(ustat, &st, sizeof(st)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_statfs64(struct trap_frame *tf)
{
	const char *upath = (const char *)tf->a0;
	struct kstatfs *ubuf = (struct kstatfs *)tf->a1;
	char *path;
	struct dentry *dentry;
	struct kstatfs st;
	int ret;

	if (!ubuf || !access_ok(ubuf, sizeof(*ubuf)))
		return -EFAULT;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = path_lookup_err(path, 0, &dentry);
	free_page(path, 0);
	if (ret < 0)
		return ret;
	if (!dentry->d_sb) {
		dput(dentry);
		return -EINVAL;
	}

	ret = vfs_statfs(dentry->d_sb, &st);
	dput(dentry);
	if (ret < 0)
		return ret;
	if (copy_to_user(ubuf, &st, sizeof(st)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_fstatfs64(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	struct kstatfs *ubuf = (struct kstatfs *)tf->a1;
	struct file *file;
	struct kstatfs st;
	int ret;

	if (!ubuf || !access_ok(ubuf, sizeof(*ubuf)))
		return -EFAULT;

	file = fd_get(fd);
	if (!file)
		return -EBADF;
	if (!file->f_inode || !file->f_inode->i_sb) {
		file_put(file);
		return -EINVAL;
	}

	ret = vfs_statfs(file->f_inode->i_sb, &st);
	file_put(file);
	if (ret < 0)
		return ret;
	if (copy_to_user(ubuf, &st, sizeof(st)) != 0)
		return -EFAULT;

	return 0;
}

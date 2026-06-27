/*
 * syscall/sys_file_helpers.c - shared file syscall ABI helpers
 */

#include <kernel/buddy.h>
#include <kernel/compiler.h>
#include <kernel/errno.h>
#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/stat.h>
#include <kernel/vfs.h>
#include <asm/page.h>

#include "sys_file_internal.h"

static_assert(VFS_PATH_MAX <= PAGE_SIZE,
	      "syscall path buffers are allocated as one page");

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

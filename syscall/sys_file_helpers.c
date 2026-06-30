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

int dirfd_path_base_path(int dfd, const char *path, struct path *basep)
{
	struct file *file;
	int ret = 0;

	if (!basep)
		return -EINVAL;
	basep->mnt = NULL;
	basep->dentry = NULL;
	if (!path)
		return -EFAULT;
	if (path[0] == '/' || dfd == AT_FDCWD)
		return 0;

	file = fd_get(dfd);
	if (!file)
		return -EBADF;
	if (!file->f_path.dentry || !file->f_path.mnt || !file->f_inode ||
	    !S_ISDIR(file->f_inode->i_mode)) {
		ret = -ENOTDIR;
		goto out;
	}

	*basep = file->f_path;
	path_get(basep);

out:
	file_put(file);
	return ret;
}

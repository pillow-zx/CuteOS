/*
 * syscall/sys_file_helpers.c - shared file syscall ABI helpers
 */

#include <kernel/buddy.h>
#include <kernel/compiler.h>
#include <kernel/errno.h>
#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/fs_struct.h>
#include <kernel/mm.h>
#include <kernel/stat.h>
#include <kernel/task.h>
#include <kernel/vfs.h>
#include <asm/page.h>

#include "sys_file_internal.h"

static_assert(VFS_PATH_MAX <= PAGE_SIZE,
	      "syscall path buffers are allocated as one page");

int copy_user_path(char **pathp, const char *user)
{
	char *dst __cleanup_with(page0) = NULL;
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
	if (len < 0)
		return (int)len;
	if (len == 0)
		return -ENOENT;

	*pathp = cleanup_take_ptr(dst);
	return 0;
}

int dirfd_path_base_path(int dfd, const char *path, struct path *basep)
{
	struct file *file __cleanup_with(file) = NULL;

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
	    !S_ISDIR(file->f_inode->i_mode))
		return -ENOTDIR;

	*basep = file->f_path;
	path_get(basep);

	return 0;
}

int copy_user_path_at(int dfd, const char *user, char **pathp,
		      struct path *basep)
{
	int ret;

	ret = copy_user_path(pathp, user);
	if (ret < 0)
		return ret;

	ret = dirfd_path_base_path(dfd, *pathp, basep);
	return ret;
}

int sys_empty_path_requested(int flags, const char *upath, bool *empty)
{
	char first;

	if (!empty)
		return -EINVAL;

	*empty = false;
	if (!(flags & AT_EMPTY_PATH))
		return 0;
	if (!upath) {
		*empty = true;
		return 0;
	}
	if (copy_from_user(&first, upath, sizeof(first)) != 0)
		return -EFAULT;
	if (first == '\0')
		*empty = true;

	return 0;
}

int sys_empty_path_inode(int dfd, struct path *cwd, struct file **filep,
			 struct inode **inodep)
{
	struct file *file;
	int ret;

	if (!inodep)
		return -EINVAL;

	*inodep = NULL;
	if (filep)
		*filep = NULL;

	if (dfd == AT_FDCWD) {
		if (!cwd)
			return -EINVAL;
		ret = fs_get_cwd_path(task_fs(current_task()), cwd);
		if (ret < 0)
			return ret;
		if (!cwd->dentry)
			return -ENOENT;
		*inodep = cwd->dentry->d_inode;
		return *inodep ? 0 : -ENOENT;
	}

	if (!filep)
		return -EINVAL;
	file = fd_get(dfd);
	if (!file)
		return -EBADF;
	*filep = file;
	*inodep = file->f_inode;
	return *inodep ? 0 : -ENOENT;
}

void sys_at_lookup_release(struct sys_at_lookup_result *lookup)
{
	if (!lookup)
		return;

	if (lookup->path)
		free_page(lookup->path, 0);
	path_put(&lookup->base);
	path_put(&lookup->found);
	path_put(&lookup->cwd);
	file_put(lookup->file);
	memset(lookup, 0, sizeof(*lookup));
}

int sys_at_lookup(struct sys_at_lookup_result *lookup, int dfd,
		  const char *upath, int at_flags, uint32_t lookup_flags,
		  bool null_is_empty)
{
	bool empty;
	int ret;

	if (!lookup)
		return -EINVAL;
	memset(lookup, 0, sizeof(*lookup));

	if (at_flags & AT_EMPTY_PATH) {
		if (!upath && !null_is_empty)
			return -EFAULT;
		ret = sys_empty_path_requested(at_flags, upath, &empty);
		if (ret < 0)
			return ret;
		if (empty) {
			ret = sys_empty_path_inode(dfd, &lookup->cwd,
						   &lookup->file,
						   &lookup->inode);
			if (ret < 0)
				return ret;
			lookup->empty_path = true;
			return 0;
		}
	}

	ret = copy_user_path_at(dfd, upath, &lookup->path, &lookup->base);
	if (ret < 0)
		return ret;

	ret = path_lookupat_path(lookup->base.dentry ? &lookup->base : NULL,
				 lookup->path, lookup_flags, &lookup->found);
	if (ret < 0)
		return ret;

	lookup->inode =
		lookup->found.dentry ? lookup->found.dentry->d_inode : NULL;
	return lookup->inode ? 0 : -ENOENT;
}

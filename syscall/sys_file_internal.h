/*
 * syscall/sys_file_internal.h - shared helpers between sys_file_*.c
 */
#ifndef SYS_FILE_INTERNAL_H
#define SYS_FILE_INTERNAL_H

#include <kernel/buddy.h>
#include <kernel/cleanup.h>
#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/types.h>
#include <kernel/vfs.h>

#define SYS_FILE_BUF_SIZE 256
#define SYS_IOV_MAX	  64

CLEANUP_DEFINE(page0, char *, if (_T) free_page(_T, 0));
CLEANUP_DEFINE(file, struct file *, if (_T) file_put(_T));
CLEANUP_DEFINE(path, struct path, if (_T.dentry || _T.mnt) path_put(&_T));

struct sys_at_lookup_result {
	char *path;
	struct path base;
	struct path found;
	struct path cwd;
	struct file *file;
	struct inode *inode;
	bool empty_path;
};

int copy_user_path(char **pathp, const char *user);
int dirfd_path_base_path(int dfd, const char *path, struct path *basep);
int copy_user_path_at(int dfd, const char *user, char **pathp,
		      struct path *basep);
int sys_empty_path_requested(int flags, const char *upath, bool *empty);
int sys_empty_path_inode(int dfd, struct path *cwd, struct file **filep,
			 struct inode **inodep);
void sys_at_lookup_release(struct sys_at_lookup_result *lookup);
int __must_check sys_at_lookup(struct sys_at_lookup_result *lookup, int dfd,
			       const char *upath, int at_flags,
			       uint32_t lookup_flags, bool null_is_empty);

CLEANUP_DEFINE(sys_at_lookup, struct sys_at_lookup_result,
	       sys_at_lookup_release(&_T));

#endif

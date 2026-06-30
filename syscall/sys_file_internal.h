/* syscall/sys_file_internal.h — shared helpers between sys_file_*.c */
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

struct sys_iovec {
	uint64_t iov_base;
	uint64_t iov_len;
};

struct linux_dirent64 {
	uint64_t d_ino;
	int64_t d_off;
	uint16_t d_reclen;
	uint8_t d_type;
	char d_name[];
};

CLEANUP_DEFINE(page0, char *, if (_T) free_page(_T, 0));
CLEANUP_DEFINE(file, struct file *, if (_T) file_put(_T));
CLEANUP_DEFINE(path, struct path, if (_T.dentry || _T.mnt) path_put(&_T));

int copy_user_path(char **pathp, const char *user);
int dirfd_path_base_path(int dfd, const char *path, struct path *basep);

#endif /* SYS_FILE_INTERNAL_H */

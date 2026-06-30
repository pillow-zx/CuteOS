/* syscall/sys_file_internal.h — shared helpers between sys_file_*.c */
#ifndef SYS_FILE_INTERNAL_H
#define SYS_FILE_INTERNAL_H

#include <kernel/fs.h>
#include <kernel/types.h>

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

int copy_user_path(char **pathp, const char *user);
int dirfd_path_base_path(int dfd, const char *path, struct path *basep);

#endif /* SYS_FILE_INTERNAL_H */

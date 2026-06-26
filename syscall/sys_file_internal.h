/* syscall/sys_file_internal.h — shared helpers between sys_file_*.c */
#ifndef SYS_FILE_INTERNAL_H
#define SYS_FILE_INTERNAL_H

#include <kernel/fs.h>

int copy_user_path(char **pathp, const char *user);
int dirfd_path_base(int dfd, const char *path, struct dentry **basep);

#endif /* SYS_FILE_INTERNAL_H */

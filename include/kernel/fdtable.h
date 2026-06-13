#ifndef _CUTEOS_KERNEL_FDTABLE_H
#define _CUTEOS_KERNEL_FDTABLE_H

/*
 * include/kernel/fdtable.h - file 引用计数与进程 fd 表
 */

#include <kernel/fs.h>

struct task_struct;

struct file *file_alloc(const struct file_operations *f_op, uint32_t mode,
			void *private_data);
struct file *file_alloc_dentry(struct dentry *dentry, uint32_t flags,
			       uint32_t mode);
void file_get(struct file *file);
void file_put(struct file *file);

int fd_alloc(struct file *file);
struct file *fd_get(int fd);
int fd_close(int fd);
int fd_dup(int oldfd);
int fd_dup2(int oldfd, int newfd);

int copy_files(struct task_struct *child);
void close_files(struct task_struct *task);
void file_install_standard_fds(struct task_struct *task);

#endif

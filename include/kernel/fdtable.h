#ifndef _CUTEOS_KERNEL_FDTABLE_H
#define _CUTEOS_KERNEL_FDTABLE_H

/*
 * include/kernel/fdtable.h - file 引用计数与共享 fd 表
 */

#include <kernel/fs.h>
#include <kernel/refcount.h>
#include <kernel/sync.h>

struct files_struct {
	refcount_t refcount;
	mutex_t lock;
	unsigned long close_on_exec;  /* bitmask: bit N = fd N has FD_CLOEXEC */
	struct file *fd[NR_OPEN];
};

struct file *file_alloc(const struct file_operations *f_op, uint32_t mode,
			void *private_data);
struct file *file_alloc_dentry(struct dentry *dentry, uint32_t flags,
			       uint32_t mode);
void file_get(struct file *file);
void file_put(struct file *file);

struct files_struct *files_alloc(void);
struct files_struct *files_dup(struct files_struct *old);
void files_get(struct files_struct *files);
void files_put(struct files_struct *files);
void files_install_standard_fds(struct files_struct *files);

int fd_alloc(struct file *file);
int fd_alloc_flags(struct file *file, int flags);
struct file *fd_get(int fd);
struct file *fd_get_checked(int fd);
int fd_close(int fd);
int fd_dup(int oldfd);
int fd_dup2(int oldfd, int newfd, int cloexec);

int init_files(struct task_struct *task);
int copy_files(struct task_struct *child, bool share);
void close_files(struct task_struct *task);

#endif

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
	unsigned long close_on_exec; /* bitmask: bit N = fd N has FD_CLOEXEC */
	struct file *fd[NR_OPEN];
};

struct file *__must_check file_alloc(const struct file_operations *f_op,
				     uint32_t mode, void *private_data);
struct file *__must_check file_alloc_path(const struct path *path,
					  uint32_t flags, uint32_t mode);
struct file *__must_check file_alloc_dentry(struct dentry *dentry,
					    uint32_t flags, uint32_t mode);
void file_get(struct file *file);
void file_put(struct file *file);

struct files_struct *__must_check files_alloc(void);
struct files_struct *__must_check files_dup(struct files_struct *old);
void files_get(struct files_struct *files);
void files_put(struct files_struct *files);
void files_install_standard_fds(struct files_struct *files);

int __must_check fd_alloc(struct file *file);
int __must_check fd_alloc_flags(struct file *file, int flags);
struct file *__must_check fd_get(int fd);
struct file *__must_check fd_get_checked(int fd);
int __must_check fd_get_close_on_exec(int fd);
int __must_check fd_set_close_on_exec(int fd, bool close_on_exec);
int fd_close(int fd);
int __must_check fd_dup(int oldfd);
int __must_check fd_dup_from(int oldfd, unsigned long minfd, int cloexec);
int __must_check fd_dup2(int oldfd, int newfd, int cloexec);

int __must_check init_files(struct task_struct *task);
int __must_check copy_files(struct task_struct *child, bool share);
void close_files(struct task_struct *task);

#endif

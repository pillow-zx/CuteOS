#ifndef _CUTEOS_KERNEL_FS_STRUCT_H
#define _CUTEOS_KERNEL_FS_STRUCT_H

/*
 * include/kernel/fs_struct.h - 可共享的进程文件系统上下文
 */

#include <kernel/refcount.h>
#include <kernel/sync.h>
#include <kernel/types.h>

struct fs_struct {
	refcount_t refcount;
	mutex_t lock;
	struct dentry *root;
	struct dentry *cwd;
	uint32_t umask;
};

struct fs_struct *__must_check fs_alloc(void);
struct fs_struct *__must_check fs_dup(struct fs_struct *old);
void fs_get(struct fs_struct *fs);
void fs_put(struct fs_struct *fs);

struct dentry *__must_check fs_get_root_dentry(struct fs_struct *fs);
struct dentry *__must_check fs_get_cwd_dentry(struct fs_struct *fs);
int __must_check fs_set_cwd(struct fs_struct *fs, struct dentry *dentry);
uint32_t __must_check fs_get_umask(struct fs_struct *fs);
uint32_t fs_set_umask(struct fs_struct *fs, uint32_t mask);
void fs_set_root_if_empty(struct fs_struct *fs, struct dentry *root);

int __must_check init_fs(struct task_struct *task);
int __must_check copy_fs(struct task_struct *child, bool share);
void exit_fs(struct task_struct *task);

#endif

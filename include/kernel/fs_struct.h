#ifndef _CUTEOS_KERNEL_FS_STRUCT_H
#define _CUTEOS_KERNEL_FS_STRUCT_H

/*
 * include/kernel/fs_struct.h - 进程文件系统上下文
 *
 * 目前只封装 cwd 引用计数。后续如果引入 root/cwd 分离或 chroot，
 * 继续在这里扩展，不让 fork/exit 直接操作 dentry 引用。
 */

struct task_struct;

void init_fs(struct task_struct *task);
void copy_fs(struct task_struct *child);
void exit_fs(struct task_struct *task);

#endif

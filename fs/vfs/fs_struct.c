/*
 * fs/vfs/fs_struct.c - 进程文件系统上下文
 */

#include <kernel/fs_struct.h>
#include <kernel/task.h>
#include <kernel/vfs.h>

void init_fs(struct task_struct *task)
{
	if (!task)
		return;

	if (current && current->cwd) {
		task->cwd = current->cwd;
		dget(task->cwd);
		return;
	}

	if (root_dentry) {
		task->cwd = root_dentry;
		dget(task->cwd);
	}
}

void copy_fs(struct task_struct *child)
{
	if (!child)
		return;

	if (child->cwd) {
		dput(child->cwd);
		child->cwd = NULL;
	}

	if (!current || !current->cwd)
		return;

	child->cwd = current->cwd;
	dget(child->cwd);
}

void exit_fs(struct task_struct *task)
{
	if (!task || !task->cwd)
		return;

	dput(task->cwd);
	task->cwd = NULL;
}

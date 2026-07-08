/*
 * syscall/sys_proc.c - 进程相关系统调用
 */

#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <kernel/exit.h>
#include <kernel/task.h>
#include <kernel/trap.h>

ssize_t sys_getpid(struct trap_frame *tf)
{
	(void)tf;
	return (ssize_t)task_tgid(current_task());
}

ssize_t sys_getppid(struct trap_frame *tf)
{
	struct task_struct *parent = task_parent(current_task());

	(void)tf;
	if (!parent)
		return 0;

	return (ssize_t)task_pid(parent);
}

ssize_t sys_getuid(struct trap_frame *tf)
{
	(void)tf;
	return task_uid(current_task());
}

ssize_t sys_geteuid(struct trap_frame *tf)
{
	(void)tf;
	return task_uid(current_task());
}

ssize_t sys_getgid(struct trap_frame *tf)
{
	(void)tf;
	return task_gid(current_task());
}

ssize_t sys_getegid(struct trap_frame *tf)
{
	(void)tf;
	return task_gid(current_task());
}

ssize_t sys_gettid(struct trap_frame *tf)
{
	(void)tf;
	return (ssize_t)task_pid(current_task());
}

/*
 * SYSCALL_SUPPORT(B): getpgid
 * Current: returns the process group for self or an existing group leader pid.
 * Unsupported errno: negative or missing pid returns -ESRCH.
 * Future: revisit when sessions and job-control semantics exist.
 */
ssize_t sys_getpgid(struct trap_frame *tf)
{
	long pid = (long)syscall_arg(tf, 0);
	struct task_struct *task;

	if (pid < 0)
		return -ESRCH;

	if (pid == 0)
		task = current_task();
	else
		task = task_find_group_leader((pid_t)pid);
	if (!task)
		return -ESRCH;

	return (ssize_t)task_pgid(task);
}

/*
 * SYSCALL_SUPPORT(B): setpgid
 * Current: updates a group leader or direct child into an existing/new pgid.
 * Unsupported errno: negative pid/pgid returns -EINVAL; non-child targets or
 * missing target groups return -EPERM.
 * Future: model sessions and shell job-control rules.
 */
ssize_t sys_setpgid(struct trap_frame *tf)
{
	long pid = (long)syscall_arg(tf, 0);
	long pgid = (long)syscall_arg(tf, 1);
	struct task_struct *self;
	struct task_struct *target;
	pid_t new_pgid;

	if (pid < 0 || pgid < 0)
		return -EINVAL;

	self = task_group_leader(current_task());
	if (pid == 0)
		target = self;
	else
		target = task_find_group_leader((pid_t)pid);
	if (!target)
		return -ESRCH;

	if (target != self && task_parent(target) != self)
		return -EPERM;

	new_pgid = pgid == 0 ? task_pid(target) : (pid_t)pgid;
	if (new_pgid != task_pid(target) && !task_pgid_exists(new_pgid))
		return -EPERM;

	task_set_pgid_all(target, new_pgid);
	return 0;
}

ssize_t sys_exit(struct trap_frame *tf)
{
	int code = (int)syscall_arg(tf, 0);
	do_exit(code);
	unreachable();
}

ssize_t sys_exit_group(struct trap_frame *tf)
{
	int code = (int)syscall_arg(tf, 0);
	do_exit_group(code);
	unreachable();
}

/*
 * syscall/sys_task.c - task lifecycle syscall ABI wrappers
 */

#include <kernel/errno.h>
#include <kernel/exit.h>
#include <kernel/fork.h>
#include <kernel/mm.h>
#include <kernel/signal.h>
#include <kernel/syscall.h>
#include <asm/trap.h>

ssize_t sys_fork(struct trap_frame *tf)
{
	return kernel_clone_from_frame(tf, SIGCHLD, 0, NULL, 0, NULL);
}

ssize_t sys_clone(struct trap_frame *tf)
{
	unsigned long flags = (unsigned long)tf->a0;
	uintptr_t child_stack = (uintptr_t)tf->a1;
	int *parent_tid = (int *)tf->a2;
	uintptr_t tls = (uintptr_t)tf->a3;
	int *child_tid = (int *)tf->a4;

	return kernel_clone_from_frame(tf, flags, child_stack, parent_tid, tls,
				       child_tid);
}

ssize_t sys_wait4(struct trap_frame *tf)
{
	pid_t pid = (pid_t)tf->a0;
	int *wstatus = (int *)tf->a1;
	int options = (int)tf->a2;
	struct wait4_result result = {0};
	int ret;

	if (wstatus && !access_ok(wstatus, sizeof(*wstatus)))
		return -EFAULT;

	ret = kernel_wait4(pid, options, &result);
	if (ret < 0)
		return ret;

	if (wstatus && copy_to_user(wstatus, &result.status,
				    sizeof(result.status)) != 0)
		return -EFAULT;

	kernel_wait4_finish(&result);
	return result.pid;
}

/*
 * syscall/sys_task.c - task lifecycle syscall ABI wrappers
 */

#include <kernel/errno.h>
#include <kernel/exit.h>
#include <kernel/fork.h>
#include <kernel/mm.h>
#include <kernel/resource.h>
#include <kernel/signal.h>
#include <kernel/syscall.h>
#include <uapi/sched.h>
#include <kernel/trap.h>

static int sys_write_tid(int *uaddr, pid_t tid)
{
	if (!uaddr)
		return -EFAULT;
	if (copy_to_user(uaddr, &tid, sizeof(tid)) != 0)
		return -EFAULT;
	return 0;
}

ssize_t sys_clone(struct trap_frame *tf)
{
	unsigned long flags = (unsigned long)syscall_arg(tf, 0);
	uintptr_t child_stack = (uintptr_t)syscall_arg(tf, 1);
	int *parent_tid = (int *)syscall_arg(tf, 2);
	uintptr_t tls = (uintptr_t)syscall_arg(tf, 3);
	int *child_tid = (int *)syscall_arg(tf, 4);
	struct kernel_clone clone;
	int ret;

	ret = kernel_clone_prepare(tf, flags, child_stack, tls, child_tid,
				   &clone);
	if (ret < 0)
		return ret;

	if (flags & CLONE_PARENT_SETTID) {
		ret = sys_write_tid(parent_tid, clone.pid);
		if (ret < 0)
			goto abort;
	}
	if (flags & CLONE_CHILD_SETTID) {
		ret = sys_write_tid(child_tid, clone.pid);
		if (ret < 0)
			goto abort;
	}

	return kernel_clone_commit(&clone);

abort:
	kernel_clone_abort(&clone);
	return ret;
}

ssize_t sys_wait4(struct trap_frame *tf)
{
	long pid = (long)syscall_arg(tf, 0);
	int *wstatus = (int *)syscall_arg(tf, 1);
	int options = (int)syscall_arg(tf, 2);
	struct rusage *urusage = (struct rusage *)syscall_arg(tf, 3);
	struct wait4_result result = {0};
	int ret;

	if (pid != -1 && pid <= 0)
		return -EINVAL;
	if (wstatus && !access_ok(wstatus, sizeof(*wstatus)))
		return -EFAULT;
	if (urusage && !access_ok(urusage, sizeof(*urusage)))
		return -EFAULT;

	ret = kernel_wait4(pid, options, &result);
	if (ret < 0)
		return ret;

	if (wstatus &&
	    copy_to_user(wstatus, &result.status, sizeof(result.status)) != 0)
		return -EFAULT;

	if (urusage) {
		struct rusage rusage;

		cputime_rusage(&result.cputime, &rusage);
		if (copy_to_user(urusage, &rusage, sizeof(rusage)) != 0)
			return -EFAULT;
	}

	kernel_wait4_finish(&result);
	return result.pid;
}

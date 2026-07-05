/*
 * kernel/fork.c - fork/clone 进程与线程创建
 */

#include <kernel/errno.h>
#include <kernel/fdtable.h>
#include <kernel/fork.h>
#include <kernel/fs_struct.h>
#include <kernel/mm.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <uapi/sched.h>

#define CLONE_EXIT_SIGNAL_MASK 0xffUL

static const unsigned long unsupported_clone_flags =
	CLONE_NEWTIME | CLONE_PTRACE | CLONE_VFORK | CLONE_PARENT |
	CLONE_NEWNS | CLONE_SYSVSEM | CLONE_NEWCGROUP | CLONE_NEWUTS |
	CLONE_NEWIPC | CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNET | CLONE_IO;
static const unsigned long thread_only_clone_flags =
	CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID | CLONE_SETTLS;

static bool clone_wants_thread(unsigned long flags)
{
	return (flags & CLONE_THREAD) != 0;
}

static int validate_clone_flags(unsigned long flags, uintptr_t child_stack)
{
	unsigned long exit_signal = flags & CLONE_EXIT_SIGNAL_MASK;

	if (flags & unsupported_clone_flags)
		return -EINVAL;
	if ((flags & CLONE_SIGHAND) && !(flags & CLONE_VM))
		return -EINVAL;
	/*
	 * Shared address spaces need an explicit child stack. Bare CLONE_VM is
	 * still unsupported for now, but CLONE_VM|CLONE_SIGHAND is allowed as
	 * the fork-like form that shares handler state without joining a thread
	 * group.
	 */
	if ((flags & CLONE_VM) && child_stack == 0)
		return -EINVAL;
	if ((flags & CLONE_VM) && !(flags & CLONE_SIGHAND))
		return -EINVAL;
	if ((flags & CLONE_THREAD) && !(flags & CLONE_VM))
		return -EINVAL;
	if ((flags & CLONE_THREAD) && !(flags & CLONE_SIGHAND))
		return -EINVAL;
	if (!clone_wants_thread(flags) && !task_is_group_leader(current_task()))
		return -EINVAL;
	if (!clone_wants_thread(flags) && (flags & thread_only_clone_flags))
		return -EINVAL;

	if (!clone_wants_thread(flags) && exit_signal != 0 &&
	    exit_signal != SIGCHLD)
		return -EINVAL;

	return 0;
}

static void child_cleanup(struct task_struct *child)
{
	struct mm_struct *mm;

	if (!child)
		return;

	close_files(child);
	exit_fs(child);
	signals_release(child);
	mm = task_mm(child);
	if (mm) {
		mm_put(mm);
		task_set_mm(child, NULL);
	}
	task_free(child);
}

static int clone_setup_mm(struct task_struct *child, unsigned long flags)
{
	struct mm_struct *mm;
	struct mm_struct *parent_mm = task_mm(current_task());

	if (flags & CLONE_VM) {
		task_set_mm(child, parent_mm);
		mm_get(parent_mm);
	} else {
		task_set_mm(child, dup_mm(parent_mm));
		if (!task_mm(child) && parent_mm)
			return -ENOMEM;
	}

	mm = task_mm(child);
	if (mm)
		task_set_satp(child, mm_user_satp(mm));

	return 0;
}

static int clone_copy_resources(struct task_struct *child, unsigned long flags)
{
	bool disable_altstack;
	int ret = copy_files(child, (bool)(flags & CLONE_FILES));
	if (ret < 0)
		return ret;

	ret = copy_fs(child, (bool)(flags & CLONE_FS));
	if (ret < 0)
		return ret;

	task_set_uid(child, task_uid(current_task()));
	task_set_gid(child, task_gid(current_task()));
	disable_altstack = (flags & CLONE_VM) && !(flags & CLONE_VFORK);
	ret = signals_clone(child, (bool)(flags & CLONE_SIGHAND),
			    clone_wants_thread(flags), disable_altstack);
	if (ret < 0)
		return ret;

	return 0;
}

static void clone_setup_task_links(struct task_struct *child,
				   unsigned long flags)
{
	child->lifecycle.exit_signal = (int)(flags & CLONE_EXIT_SIGNAL_MASK);
	task_set_pgid(child, task_pgid(current_task()));
	if (clone_wants_thread(flags)) {
		struct task_struct *leader = task_group_leader(current_task());

		child->ids.tgid = task_tgid(current_task());
		child->ids.group_leader = leader;
		child->lifecycle.exit_signal = 0;
		child->links.parent = leader;
		return;
	}

	child->ids.tgid = child->ids.pid;
	child->ids.group_leader = child;
	child->links.parent = current_task();
}

static void clone_link_task(struct task_struct *child, unsigned long flags)
{
	if (clone_wants_thread(flags))
		list_add_tail(
			&child->links.thread_node,
			&task_group_leader(current_task())->links.thread_group);
	else
		list_add(&child->links.sibling, task_children(current_task()));
}

int kernel_clone_prepare(struct trap_frame *tf, unsigned long flags,
			 uintptr_t child_stack, uintptr_t tls,
			 int *clear_child_tid, struct kernel_clone *clone)
{
	struct task_struct *child;
	int ret;

	if (!clone)
		return -EINVAL;
	memset(clone, 0, sizeof(*clone));

	ret = validate_clone_flags(flags, child_stack);
	if (ret < 0)
		return ret;

	child = task_alloc();
	if (!child)
		return -ENOMEM;

	ret = clone_setup_mm(child, flags);
	if (ret < 0) {
		child_cleanup(child);
		return ret;
	}

	arch_task_setup_clone_frame(child, tf, flags, child_stack, tls);

	ret = clone_copy_resources(child, flags);
	if (ret < 0) {
		child_cleanup(child);
		return ret;
	}

	if (flags & CLONE_CHILD_CLEARTID)
		task_set_clear_child_tid(child, clear_child_tid);

	clone_setup_task_links(child, flags);
	clone->task = child;
	clone->flags = flags;
	clone->pid = task_pid(child);
	return 0;
}

pid_t kernel_clone_commit(struct kernel_clone *clone)
{
	struct task_struct *child;

	if (!clone || !clone->task)
		return -EINVAL;

	child = clone->task;
	clone_link_task(child, clone->flags);
	sched_enqueue(child);
	clone->task = NULL;
	return clone->pid;
}

void kernel_clone_abort(struct kernel_clone *clone)
{
	if (!clone || !clone->task)
		return;

	child_cleanup(clone->task);
	clone->task = NULL;
}

ssize_t kernel_clone_from_frame(struct trap_frame *tf, unsigned long flags,
				uintptr_t child_stack, int *parent_tid,
				uintptr_t tls, int *child_tid)
{
	struct kernel_clone clone;
	int ret;

	(void)parent_tid;
	(void)child_tid;

	ret = kernel_clone_prepare(tf, flags, child_stack, tls, NULL, &clone);
	if (ret < 0)
		return ret;

	return kernel_clone_commit(&clone);
}

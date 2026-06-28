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
#include <kernel/string.h>
#include <kernel/task.h>
#include <uapi/sched.h>
#include <asm/csr.h>
#include <asm/page.h>
#include <asm/trap.h>

#define CLONE_EXIT_SIGNAL_MASK 0xffUL

static const unsigned long unsupported_clone_flags =
	CLONE_NEWTIME | CLONE_PTRACE | CLONE_VFORK | CLONE_PARENT |
	CLONE_NEWNS | CLONE_SYSVSEM | CLONE_NEWCGROUP | CLONE_NEWUTS |
	CLONE_NEWIPC | CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNET |
	CLONE_IO;
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
	if (!clone_wants_thread(flags) && !task_is_group_leader(current))
		return -EINVAL;
	if (!clone_wants_thread(flags) && (flags & thread_only_clone_flags))
		return -EINVAL;

	if (!clone_wants_thread(flags) &&
	    exit_signal != 0 && exit_signal != SIGCHLD)
		return -EINVAL;

	return 0;
}

static int write_user_tid(int *uaddr, pid_t tid)
{
	if (!uaddr)
		return -EFAULT;
	if (copy_to_user(uaddr, &tid, sizeof(tid)) != 0)
		return -EFAULT;
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
	struct mm_struct *parent_mm = task_mm(current);

	if (flags & CLONE_VM) {
		task_set_mm(child, parent_mm);
		mm_get(parent_mm);
	} else {
		task_set_mm(child, dup_mm(parent_mm));
		if (!task_mm(child) && parent_mm)
			return -ENOMEM;
	}

	mm = task_mm(child);
	if (mm) {
		paddr_t pgd_pa = __pa((uintptr_t)mm->pgd);
		task_set_satp(child, SATP_MODE_SV39 | (pgd_pa >> PAGE_SHIFT));
	}

	return 0;
}

static void clone_setup_frame(struct task_struct *child, struct trap_frame *tf,
			      unsigned long flags, uintptr_t child_stack,
			      uintptr_t tls)
{
	struct trap_frame *child_tf =
		(struct trap_frame *)((uint8_t *)child->kstack + KSTACK_SIZE -
				      sizeof(struct trap_frame));

	memcpy(child_tf, tf, sizeof(struct trap_frame));
	child_tf->a0 = 0;
	if (child_stack != 0)
		child_tf->sp = child_stack;
	if (flags & CLONE_SETTLS)
		child_tf->tp = tls;

	task_set_trap_frame(child, child_tf);
	child->ctx.ra = (size_t)__trapret;
	child->ctx.sp = (size_t)child_tf;
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

	task_set_uid(child, task_uid(current));
	task_set_gid(child, task_gid(current));
	disable_altstack = (flags & CLONE_VM) && !(flags & CLONE_VFORK);
	ret = signals_clone(child, (bool)(flags & CLONE_SIGHAND),
			    clone_wants_thread(flags), disable_altstack);
	if (ret < 0)
		return ret;

	return 0;
}

static void clone_link_task(struct task_struct *child, unsigned long flags)
{
	child->exit_signal = (int)(flags & CLONE_EXIT_SIGNAL_MASK);
	if (clone_wants_thread(flags)) {
		struct task_struct *leader = task_group_leader(current);

		child->tgid = task_tgid(current);
		child->group_leader = leader;
		child->exit_signal = 0;
		child->parent = leader;
		list_add_tail(&child->thread_node, &leader->thread_group);
		return;
	}

	child->tgid = child->pid;
	child->group_leader = child;
	child->parent = current;
	list_add(&child->sibling, task_children(current));
}

static void clone_unlink_task(struct task_struct *child)
{
	if (!list_empty(&child->thread_node))
		list_del_init(&child->thread_node);
	if (!list_empty(&child->sibling))
		list_del_init(&child->sibling);
}

static int clone_write_tid_results(struct task_struct *child,
				   unsigned long flags, int *parent_tid,
				   int *child_tid)
{
	int ret;

	if (flags & CLONE_CHILD_CLEARTID)
		task_set_clear_child_tid(child, child_tid);
	if (flags & CLONE_PARENT_SETTID) {
		ret = write_user_tid(parent_tid, task_pid(child));
		if (ret < 0)
			return ret;
	}
	if (flags & CLONE_CHILD_SETTID) {
		ret = write_user_tid(child_tid, task_pid(child));
		if (ret < 0)
			return ret;
	}

	return 0;
}

ssize_t kernel_clone_from_frame(struct trap_frame *tf, unsigned long flags,
				uintptr_t child_stack, int *parent_tid,
				uintptr_t tls, int *child_tid)
{
	int ret = validate_clone_flags(flags, child_stack);
	if (ret < 0)
		return ret;

	struct task_struct *child = task_alloc();
	if (!child)
		return -ENOMEM;

	ret = clone_setup_mm(child, flags);
	if (ret < 0) {
		child_cleanup(child);
		return ret;
	}

	clone_setup_frame(child, tf, flags, child_stack, tls);

	ret = clone_copy_resources(child, flags);
	if (ret < 0) {
		child_cleanup(child);
		return ret;
	}

	clone_link_task(child, flags);
	ret = clone_write_tid_results(child, flags, parent_tid, child_tid);
	if (ret < 0)
		goto fail_after_link;

	sched_enqueue(child);
	return task_pid(child);

fail_after_link:
	clone_unlink_task(child);
	child_cleanup(child);
	return ret;
}

/*
 * syscall/sys_stub.c - 当前内核子系统尚不足的系统调用占位实现
 */

#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/rseq.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/trap.h>

#define SINGLE_CPU_AFFINITY_MASK 1UL

static struct task_struct *affinity_target_task(pid_t pid)
{
	if (pid == 0)
		return current_task();

	return task_find_thread(pid);
}

/*
 * SYSCALL_SUPPORT(C): sched_setaffinity
 * Current: accepts masks that include CPU0 on the single online CPU.
 * Unsupported errno: empty CPU set returns -EINVAL; missing target returns
 * -ESRCH; unauthorized cross-user target returns -EPERM.
 * Future: store per-task affinity when SMP support exists.
 */
ssize_t sys_sched_setaffinity(struct trap_frame *tf)
{
	long pid = (long)syscall_arg(tf, 0);
	size_t cpusetsize = (size_t)syscall_arg(tf, 1);
	const unsigned long *umask = (const unsigned long *)syscall_arg(tf, 2);
	unsigned long mask = 0;
	size_t copy_size;
	struct task_struct *task;

	if (pid < 0)
		return -ESRCH;
	if (cpusetsize == 0)
		return -EINVAL;
	if (!umask)
		return -EFAULT;

	task = affinity_target_task(pid);
	if (!task)
		return -ESRCH;
	if (task != current_task() && current_task() &&
	    task_uid(current_task()) != 0 &&
	    task_uid(current_task()) != task_uid(task))
		return -EPERM;

	copy_size = cpusetsize < sizeof(mask) ? cpusetsize : sizeof(mask);
	if (copy_from_user(&mask, umask, copy_size) != 0)
		return -EFAULT;
	if ((mask & SINGLE_CPU_AFFINITY_MASK) == 0)
		return -EINVAL;

	return 0;
}

/*
 * SYSCALL_SUPPORT(C): sched_getaffinity
 * Current: reports a fixed CPU0 mask for any existing target task.
 * Unsupported errno: too-small cpusetsize returns -EINVAL; missing target
 * returns -ESRCH.
 * Future: return stored per-task affinity when SMP support exists.
 */
ssize_t sys_sched_getaffinity(struct trap_frame *tf)
{
	long pid = (long)syscall_arg(tf, 0);
	size_t cpusetsize = (size_t)syscall_arg(tf, 1);
	unsigned long *umask = (unsigned long *)syscall_arg(tf, 2);
	unsigned long mask = SINGLE_CPU_AFFINITY_MASK;
	struct task_struct *task;

	if (pid < 0)
		return -ESRCH;
	if (cpusetsize < sizeof(mask))
		return -EINVAL;
	if (!umask)
		return -EFAULT;

	task = affinity_target_task(pid);
	if (!task)
		return -ESRCH;

	if (copy_to_user(umask, &mask, sizeof(mask)) != 0)
		return -EFAULT;

	return (ssize_t)sizeof(mask);
}

/*
 * SYSCALL_SUPPORT(B): rseq
 * Current: supports single-core register, unregister, resume, and abort paths.
 * Unsupported errno: unknown flags return -EINVAL; signature mismatch returns
 * -EPERM; duplicate matching registration returns -EBUSY.
 * Future: define flag, migration, and mm_cid policy before SMP work.
 */
ssize_t sys_rseq(struct trap_frame *tf)
{
	struct rseq *area = (struct rseq *)syscall_arg(tf, 0);
	uint32_t len = (uint32_t)syscall_arg(tf, 1);
	int flags = (int)syscall_arg(tf, 2);
	uint32_t sig = (uint32_t)syscall_arg(tf, 3);

	return kernel_rseq(area, len, flags, sig);
}

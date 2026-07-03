/*
 * syscall/sys_stub.c - 当前内核子系统尚不足的系统调用占位实现
 *
 * 每个函数保留 Linux riscv64 ABI 入口和明确 TODO，等对应子系统成熟后
 * 再补完整语义。
 */

#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <asm/trap.h>

#define SINGLE_CPU_AFFINITY_MASK 1UL

static struct task_struct *affinity_target_task(pid_t pid)
{
	if (pid == 0)
		return current;

	return task_find_thread(pid);
}

ssize_t sys_epoll_create1(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(epoll): 需要 pollable file、等待队列聚合和 epoll fd 对象。 */
	return -ENOSYS;
}

ssize_t sys_epoll_ctl(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(epoll): 需要 epoll interest list 和 ready list。 */
	return -ENOSYS;
}

ssize_t sys_epoll_pwait(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(epoll): 需要可被信号中断的等待和事件复制语义。 */
	return -ENOSYS;
}

ssize_t sys_sched_setaffinity(struct trap_frame *tf)
{
	long pid = (long)tf->a0;
	size_t cpusetsize = (size_t)tf->a1;
	const unsigned long *umask = (const unsigned long *)tf->a2;
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
	if (task != current && current && task_uid(current) != 0 &&
	    task_uid(current) != task_uid(task))
		return -EPERM;

	copy_size = cpusetsize < sizeof(mask) ? cpusetsize : sizeof(mask);
	if (copy_from_user(&mask, umask, copy_size) != 0)
		return -EFAULT;
	if ((mask & SINGLE_CPU_AFFINITY_MASK) == 0)
		return -EINVAL;

	return 0;
}

ssize_t sys_sched_getaffinity(struct trap_frame *tf)
{
	long pid = (long)tf->a0;
	size_t cpusetsize = (size_t)tf->a1;
	unsigned long *umask = (unsigned long *)tf->a2;
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

ssize_t sys_setpgid(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(proc): 需要进程组/session/job-control 模型。 */
	return -ENOSYS;
}

ssize_t sys_getpgid(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(proc): 需要进程组/session/job-control 模型。 */
	return -ENOSYS;
}

ssize_t sys_getrusage(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(proc): 需要 rusage ABI 结构和资源计费汇总。 */
	return -ENOSYS;
}

ssize_t sys_mlock(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(mm): 需要 page pin/unevictable 语义；当前也没有换页。 */
	return -ENOSYS;
}

ssize_t sys_munlock(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(mm): 需要 page pin/unevictable 语义；当前也没有换页。 */
	return -ENOSYS;
}

ssize_t sys_rseq(struct trap_frame *tf)
{
	(void)tf;
	/*
	 * Compatibility policy: report rseq unsupported so libc falls back to
	 * normal atomics. Returning success without full
	 * abort-on-preempt/signal semantics would be more dangerous than an
	 * explicit -ENOSYS.
	 */
	return -ENOSYS;
}

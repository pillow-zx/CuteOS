/*
 * syscall/sys_futex.c - futex and robust-list syscall ABI wrappers
 */

#include <kernel/errno.h>
#include <kernel/futex.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/trap.h>

static int futex_copy_timeout(const struct timespec *utimeout,
			      struct futex_deadline *deadline)
{
	struct timespec timeout;
	int ret;

	if (!deadline)
		return -EINVAL;

	deadline->active = false;
	deadline->expires = 0;
	if (!utimeout)
		return 0;

	if (copy_from_user(&timeout, utimeout, sizeof(timeout)) != 0)
		return -EFAULT;

	ret = mtime_deadline_from_timespec(&timeout, &deadline->active,
					   &deadline->expires);
	return ret;
}

/*
 * SYSCALL_SUPPORT(B): futex
 * Current: supports FUTEX_WAIT/FUTEX_WAKE, PRIVATE aliases, and robust-list
 * exit wakeups.
 * Unsupported errno: realtime timeout, requeue, PI, bitset, and unknown ops
 * return -ENOSYS from kernel_futex().
 * Future: add requeue, PI, and bitset ops only by pthread/libc demand.
 */
ssize_t sys_futex(struct trap_frame *tf)
{
	int *uaddr = (int *)syscall_arg(tf, 0);
	int op = (int)syscall_arg(tf, 1);
	int val = (int)syscall_arg(tf, 2);
	const struct timespec *timeout = (const struct timespec *)syscall_arg(tf, 3);
	struct futex_deadline deadline;
	int ret;

	deadline.active = false;
	deadline.expires = 0;
	if ((op & FUTEX_CMD_MASK) == FUTEX_WAIT &&
	    !(op & FUTEX_CLOCK_REALTIME)) {
		ret = futex_copy_timeout(timeout, &deadline);
		if (ret < 0)
			return ret;
	}

	return kernel_futex(uaddr, op, val, &deadline);
}

/*
 * SYSCALL_SUPPORT(B): set_robust_list
 * Current: records the current task robust-list head for exit-time walking.
 * Unsupported errno: len different from struct robust_list_head returns
 * -EINVAL.
 * Future: keep this stable while adding invalid-chain stress coverage.
 */
ssize_t sys_set_robust_list(struct trap_frame *tf)
{
	struct robust_list_head *head = (struct robust_list_head *)syscall_arg(tf, 0);
	size_t len = (size_t)syscall_arg(tf, 1);

	return futex_set_robust_list(current_task(), head, len);
}

/*
 * SYSCALL_SUPPORT(B): get_robust_list
 * Current: queries pid 0 or an existing thread's robust-list pointer and len.
 * Unsupported errno: negative pid returns -EINVAL; missing task returns -ESRCH;
 * cross-thread permission checks are shallow.
 * Future: add permission behavior when credentials are deepened.
 */
ssize_t sys_get_robust_list(struct trap_frame *tf)
{
	long pid = (long)syscall_arg(tf, 0);
	struct robust_list_head **uhead = (struct robust_list_head **)syscall_arg(tf, 1);
	size_t *ulen = (size_t *)syscall_arg(tf, 2);
	struct task_struct *task;
	struct robust_list_head *head;
	size_t len;
	int ret;

	if (!uhead || !ulen)
		return -EFAULT;
	if (pid < 0)
		return -EINVAL;

	task = pid == 0 ? current_task() : task_find_thread((pid_t)pid);
	if (!task)
		return -ESRCH;

	ret = futex_get_robust_list(task, &head, &len);
	if (ret < 0)
		return ret;
	if (copy_to_user(uhead, &head, sizeof(head)) != 0)
		return -EFAULT;
	if (copy_to_user(ulen, &len, sizeof(len)) != 0)
		return -EFAULT;

	return 0;
}

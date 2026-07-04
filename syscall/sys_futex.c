/*
 * syscall/sys_futex.c - futex and robust-list syscall ABI wrappers
 */

#include <kernel/errno.h>
#include <kernel/futex.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <asm/trap.h>

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

ssize_t sys_futex(struct trap_frame *tf)
{
	int *uaddr = (int *)tf->a0;
	int op = (int)tf->a1;
	int val = (int)tf->a2;
	const struct timespec *timeout =
		(const struct timespec *)tf->a3;
	struct futex_deadline deadline;
	int ret;

	deadline.active = false;
	deadline.expires = 0;
	if ((op & FUTEX_CMD_MASK) == FUTEX_WAIT) {
		ret = futex_copy_timeout(timeout, &deadline);
		if (ret < 0)
			return ret;
	}

	return kernel_futex(uaddr, op, val, &deadline);
}

ssize_t sys_set_robust_list(struct trap_frame *tf)
{
	struct robust_list_head *head = (struct robust_list_head *)tf->a0;
	size_t len = (size_t)tf->a1;

	return futex_set_robust_list(current, head, len);
}

ssize_t sys_get_robust_list(struct trap_frame *tf)
{
	long pid = (long)tf->a0;
	struct robust_list_head **uhead = (struct robust_list_head **)tf->a1;
	size_t *ulen = (size_t *)tf->a2;
	struct task_struct *task;
	struct robust_list_head *head;
	size_t len;
	int ret;

	if (!uhead || !ulen)
		return -EFAULT;
	if (pid < 0)
		return -EINVAL;

	task = pid == 0 ? current : task_find_thread((pid_t)pid);
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

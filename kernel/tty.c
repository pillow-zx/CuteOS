/*
 * kernel/tty.c - 最小 TTY/session 策略 helper
 */

#include <kernel/errno.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/tty.h>

static struct task_struct *tty_signal_target(void)
{
	struct task_struct *leader;

	if (!current_task())
		return NULL;

	leader = task_group_leader(current_task());
	if (leader)
		return leader;
	return current_task();
}

int tty_deliver_signal(int sig)
{
	struct task_struct *target;

	if (!signal_is_valid(sig))
		return -EINVAL;

	target = tty_signal_target();
	if (!target)
		return -ESRCH;

	return send_group_signal(sig, target);
}

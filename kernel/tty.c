/*
 * kernel/tty.c - 最小 TTY/session 策略 helper
 */

#include <kernel/errno.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/tty.h>
#include <uapi/signal.h>

static pid_t console_session;
static pid_t console_foreground_pgid;

static struct task_struct *tty_current_leader(void)
{
	struct task_struct *leader;

	if (!current_task())
		return NULL;

	leader = task_group_leader(current_task());
	if (leader)
		return leader;
	return current_task();
}

static bool tty_console_has_owner(void)
{
	if (console_session <= 0)
		return false;
	if (!task_sid_exists(console_session)) {
		console_session = 0;
		console_foreground_pgid = 0;
		return false;
	}

	return true;
}

static bool tty_console_owned_by_current(void)
{
	if (!current_task())
		return false;
	if (!tty_console_has_owner())
		return false;

	return task_sid(current_task()) == console_session;
}

void tty_console_init_session(struct task_struct *task)
{
	struct task_struct *leader = task_group_leader_safe(task);

	if (!leader)
		return;

	console_session = task_sid(leader);
	console_foreground_pgid = task_pgid(leader);
}

int tty_console_acquire(int steal)
{
	struct task_struct *leader = tty_current_leader();

	if (!leader)
		return -ESRCH;
	if (task_sid(leader) != task_pid(leader))
		return -EPERM;
	if (tty_console_has_owner() && console_session != task_sid(leader)) {
		if (steal != 1)
			return -EPERM;
		return -EPERM;
	}

	console_session = task_sid(leader);
	console_foreground_pgid = task_pgid(leader);
	return 0;
}

int tty_console_release(void)
{
	pid_t old_foreground;
	bool was_leader;

	if (!current_task())
		return -ESRCH;
	if (!tty_console_owned_by_current())
		return -ENOTTY;

	old_foreground = console_foreground_pgid;
	was_leader = task_sid(current_task()) == task_pid(tty_current_leader());
	console_session = 0;
	console_foreground_pgid = 0;

	if (was_leader && old_foreground > 0) {
		(void)send_pgrp_signal(SIGHUP, old_foreground);
		(void)send_pgrp_signal(SIGCONT, old_foreground);
	}

	return 0;
}

int tty_console_get_foreground_pgid(pid_t *pgid)
{
	if (!pgid)
		return -EINVAL;
	if (!tty_console_owned_by_current())
		return -ENOTTY;

	*pgid = console_foreground_pgid;
	return 0;
}

int tty_console_set_foreground_pgid(pid_t pgid)
{
	if (!tty_console_owned_by_current())
		return -ENOTTY;
	if (pgid <= 0)
		return -EINVAL;
	if (!task_pgid_in_session(pgid, console_session))
		return -EPERM;

	console_foreground_pgid = pgid;
	return 0;
}

int tty_console_get_sid(pid_t *sid)
{
	if (!sid)
		return -EINVAL;
	if (!tty_console_owned_by_current())
		return -ENOTTY;

	*sid = console_session;
	return 0;
}

int tty_deliver_signal(int sig)
{
	struct task_struct *target;

	if (!signal_is_valid(sig))
		return -EINVAL;

	if (console_foreground_pgid > 0)
		return send_pgrp_signal(sig, console_foreground_pgid);

	target = tty_current_leader();
	if (!target)
		return -ESRCH;

	return send_group_signal(sig, target);
}

/*
 * kernel/session.c - process-session and controlling-TTY coordination
 */

#include <kernel/errno.h>
#include <kernel/session.h>
#include <kernel/signal.h>
#include <kernel/mutex.h>
#include <kernel/task.h>
#include <uapi/signal.h>

#include "task_internal.h"
#include "tty_internal.h"

static DEFINE_MUTEX(session_lock);

static struct task_struct *session_task_leader(struct task_struct *task)
{
	return task_group_leader_safe(task);
}

static void session_signal_hangup(const struct tty_ctty_state *detached)
{
	if (!detached || detached->sid <= 0 || detached->foreground_pgid <= 0)
		return;
	(void)send_session_pgrp_signal(SIGHUP, detached->foreground_pgid,
				       detached->sid);
	(void)send_session_pgrp_signal(SIGCONT, detached->foreground_pgid,
				       detached->sid);
}

static void session_clear_empty_foreground(pid_t sid, pid_t pgid,
					   const struct task_struct *exclude)
{
	if (sid <= 0 || pgid <= 0)
		return;
	if (!task_pgid_has_live_member_except(pgid, sid, exclude))
		tty_ctty_clear_foreground_if(sid, pgid);
}

int session_process_clone_prepare(struct task_struct *child,
				  struct task_struct *parent,
				  bool share_thread_group)
{
	int ret;

	if (!child || !parent)
		return -EINVAL;

	mutex_lock(&session_lock);
	ret = task_process_clone_identity(child, parent);
	if (ret == 0 && !share_thread_group)
		ret = tty_ctty_clone_attachment(parent, child);
	mutex_unlock(&session_lock);
	return ret;
}

int session_process_setsid(struct task_struct *task)
{
	struct task_struct *leader = session_task_leader(task);
	struct task_process_identity old_identity;
	int ret;

	if (!leader)
		return -ESRCH;

	mutex_lock(&session_lock);
	ret = task_process_setsid(leader, &old_identity);
	if (ret > 0) {
		tty_ctty_remove_task(leader, old_identity.sid,
				     TTY_CTTY_REMOVE_TASK, NULL);
		session_clear_empty_foreground(old_identity.sid,
					       old_identity.pgid, NULL);
	}
	mutex_unlock(&session_lock);
	return ret;
}

int session_process_setpgid(pid_t pid, pid_t pgid)
{
	struct task_process_identity old_identity;
	int ret;

	mutex_lock(&session_lock);
	ret = task_process_setpgid(current_task(), pid, pgid, &old_identity);
	if (ret == 0)
		session_clear_empty_foreground(old_identity.sid,
					       old_identity.pgid, NULL);
	mutex_unlock(&session_lock);
	return ret;
}

static void session_process_cleanup(struct task_struct *task)
{
	struct task_process_identity identity;
	struct tty_ctty_state detached;

	if (!task_is_group_leader(task))
		return;

	mutex_lock(&session_lock);
	if (task_process_snapshot(task, &identity) == 0) {
		tty_ctty_remove_task(task, identity.sid,
				     identity.sid == task_pid(task)
					     ? TTY_CTTY_REVOKE_SESSION
					     : TTY_CTTY_REMOVE_TASK,
				     &detached);
		if (detached.sid == 0)
			session_clear_empty_foreground(identity.sid,
						       identity.pgid, task);
	} else {
		detached.sid = 0;
		detached.foreground_pgid = 0;
	}
	mutex_unlock(&session_lock);
	session_signal_hangup(&detached);
}

void session_process_exit(struct task_struct *task)
{
	session_process_cleanup(task);
}

void session_process_abort(struct task_struct *task)
{
	session_process_cleanup(task);
}

int session_console_acquire(int steal)
{
	struct task_struct *leader = session_task_leader(current_task());
	struct tty_endpoint *tty = tty_console_endpoint();
	struct task_process_identity identity;
	struct tty_ctty_state owner;
	struct tty_ctty_state displaced;
	bool has_owner;
	int ret;

	if (!leader)
		return -ESRCH;

	mutex_lock(&session_lock);
	ret = task_process_snapshot(leader, &identity);
	if (ret < 0)
		goto out;
	if (identity.sid != task_pid(leader)) {
		ret = -EPERM;
		goto out;
	}
	has_owner = tty_ctty_has_owner(tty, &owner);
	if (has_owner && owner.sid != identity.sid &&
	    (steal != 1 || task_uid(current_task()) != 0)) {
		ret = -EPERM;
		goto out;
	}
	ret = tty_ctty_claim(tty, leader, identity.sid, identity.pgid,
			     has_owner && owner.sid != identity.sid,
			     &displaced);
out:
	mutex_unlock(&session_lock);
	if (ret == 0)
		session_signal_hangup(&displaced);
	return ret;
}

int session_console_release(void)
{
	struct task_struct *leader = session_task_leader(current_task());
	struct tty_endpoint *tty = tty_console_endpoint();
	struct task_process_identity identity;
	struct tty_ctty_state detached;
	int ret;

	if (!leader)
		return -ESRCH;

	mutex_lock(&session_lock);
	ret = task_process_snapshot(leader, &identity);
	if (ret < 0)
		goto out;
	if (!tty_ctty_owned_by(tty, leader, identity.sid)) {
		ret = -ENOTTY;
		goto out;
	}
	tty_ctty_remove_task(leader, identity.sid,
			     identity.sid == task_pid(leader)
				     ? TTY_CTTY_REVOKE_SESSION
				     : TTY_CTTY_REMOVE_TASK,
			     &detached);
	ret = 0;
out:
	mutex_unlock(&session_lock);
	if (ret == 0)
		session_signal_hangup(&detached);
	return ret;
}

int session_console_get_foreground_pgid(pid_t *pgid)
{
	struct task_struct *leader = session_task_leader(current_task());
	struct tty_endpoint *tty = tty_console_endpoint();
	struct task_process_identity identity;
	int ret;

	if (!leader)
		return -ESRCH;

	mutex_lock(&session_lock);
	ret = task_process_snapshot(leader, &identity);
	if (ret == 0)
		ret = tty_ctty_get_foreground_pgid(tty, leader, identity.sid,
						   pgid);
	mutex_unlock(&session_lock);
	return ret;
}

int session_console_set_foreground_pgid(pid_t pgid)
{
	struct task_struct *leader = session_task_leader(current_task());
	struct tty_endpoint *tty = tty_console_endpoint();
	struct task_process_identity identity;
	int ret;

	if (!leader)
		return -ESRCH;

	mutex_lock(&session_lock);
	ret = task_process_snapshot(leader, &identity);
	if (ret < 0)
		goto out;
	if (!tty_ctty_owned_by(tty, leader, identity.sid)) {
		ret = -ENOTTY;
		goto out;
	}
	if (pgid <= 0) {
		ret = -EINVAL;
		goto out;
	}
	if (!task_pgid_has_live_member_except(pgid, identity.sid, NULL)) {
		ret = -EPERM;
		goto out;
	}
	ret = tty_ctty_set_foreground_pgid(tty, leader, identity.sid, pgid);
out:
	mutex_unlock(&session_lock);
	return ret;
}

int session_console_get_sid(pid_t *sid)
{
	struct task_struct *leader = session_task_leader(current_task());
	struct tty_endpoint *tty = tty_console_endpoint();
	struct task_process_identity identity;
	int ret;

	if (!leader)
		return -ESRCH;
	if (!sid)
		return -EINVAL;

	mutex_lock(&session_lock);
	ret = task_process_snapshot(leader, &identity);
	if (ret == 0 && !tty_ctty_owned_by(tty, leader, identity.sid))
		ret = -ENOTTY;
	if (ret == 0)
		*sid = identity.sid;
	mutex_unlock(&session_lock);
	return ret;
}

int session_console_deliver_foreground_signal(int sig)
{
	struct tty_endpoint *tty = tty_console_endpoint();
	struct tty_ctty_state foreground;
	int ret;

	if (!signal_is_valid(sig))
		return -EINVAL;

	mutex_lock(&session_lock);
	ret = tty_ctty_snapshot_foreground(tty, &foreground);
	mutex_unlock(&session_lock);
	if (ret < 0)
		return ret;
	return send_session_pgrp_signal(sig, foreground.foreground_pgid,
					foreground.sid);
}

/*
 * syscall/sys_log.c - syslog ABI compatibility wrapper
 *
 * This file intentionally implements only the probe-safe syslog(2) subset.
 * printk does not yet keep a readable ring buffer, so read/clear operations
 * remain explicit TODOs instead of returning fabricated log data.
 */

#include <kernel/errno.h>
#include <kernel/printk.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <uapi/syslog.h>
#include <asm/trap.h>

static inline bool syslog_action_valid(int type)
{
	return type >= SYSLOG_ACTION_CLOSE && type <= SYSLOG_ACTION_SIZE_BUFFER;
}

static bool syslog_action_requires_root(int type)
{
	switch (type) {
	case SYSLOG_ACTION_CLOSE:
	case SYSLOG_ACTION_OPEN:
	case SYSLOG_ACTION_READ:
	case SYSLOG_ACTION_SIZE_BUFFER:
		return false;
	default:
		return true;
	}
}

ssize_t sys_syslog(struct trap_frame *tf)
{
	int type = (int)tf->a0;

	if (!syslog_action_valid(type))
		return -EINVAL;
	if (syslog_action_requires_root(type) && task_uid(current) != 0)
		return -EPERM;

	switch (type) {
	case SYSLOG_ACTION_SIZE_BUFFER:
		return (ssize_t)log_buffer_size();
	default:
		/* TODO(log): implement printk ring-buffer read/clear semantics.
		 */
		return -ENOSYS;
	}
}

/*
 * syscall/sys_file_poll.c - ppoll 系统调用
 *
 * 覆盖范围：
 *   超时时间戳转换（sys_timespec_to_deadline）、fd 就绪扫描（ppoll_scan）、
 *   以及带信号掩码的 ppoll 等待循环。
 */

#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/signal.h>
#include <kernel/types.h>
#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/vfs.h>
#include <asm/page.h>
#include <asm/trap.h>
#include <kernel/time.h>

struct sys_pollfd {
	int32_t fd;
	int16_t events;
	int16_t revents;
};

struct ppoll_scan_ctx {
	struct sys_pollfd *fds;
	size_t nfds;
};

static int sys_timespec_to_deadline(const struct timespec *ts,
				    bool *has_timeout, uint64_t *deadline)
{
	uint64_t now;
	uint64_t delta;
	uint64_t nsec_delta;

	*has_timeout = false;
	*deadline = 0;
	if (!ts)
		return 0;
	if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000LL)
		return -EINVAL;

	now = arch_timer_now();
	if ((uint64_t)ts->tv_sec > (UINT64_MAX - now) / MTIME_FREQ) {
		*deadline = UINT64_MAX;
		*has_timeout = true;
		return 0;
	}

	delta = (uint64_t)ts->tv_sec * MTIME_FREQ;
	nsec_delta = ((uint64_t)ts->tv_nsec * MTIME_FREQ + 999999999ULL) /
		     1000000000ULL;
	if (nsec_delta > UINT64_MAX - now - delta)
		*deadline = UINT64_MAX;
	else
		*deadline = now + delta + nsec_delta;
	*has_timeout = true;
	return 0;
}

static int ppoll_scan(struct vfs_poll_table *table, void *arg)
{
	struct ppoll_scan_ctx *ctx = arg;
	int ready = 0;

	for (size_t i = 0; i < ctx->nfds; i++) {
		struct file *file;
		uint32_t mask;

		ctx->fds[i].revents = 0;
		if (ctx->fds[i].fd < 0)
			continue;

		file = fd_get(ctx->fds[i].fd);
		if (!file) {
			ctx->fds[i].revents = POLLNVAL;
			ready++;
			continue;
		}

		mask = vfs_poll(file, (uint32_t)ctx->fds[i].events, table);
		file_put(file);
		ctx->fds[i].revents =
			(int16_t)(mask & (ctx->fds[i].events | POLLERR |
					  POLLHUP | POLLNVAL));
		if (ctx->fds[i].revents)
			ready++;
	}

	return ready;
}

ssize_t sys_ppoll(struct trap_frame *tf)
{
	struct sys_pollfd *ufds = (struct sys_pollfd *)tf->a0;
	size_t nfds = (size_t)tf->a1;
	const struct timespec *utimeout =
		(const struct timespec *)tf->a2;
	const uint64_t *usigmask = (const uint64_t *)tf->a3;
	size_t sigsetsize = (size_t)tf->a4;
	struct sys_pollfd fds[NR_OPEN];
	struct timespec timeout;
	struct ppoll_scan_ctx scan_ctx;
	bool has_timeout;
	uint64_t deadline;
	uint64_t old_blocked = 0;
	bool swapped_mask = false;
	int ret;

	if (nfds > NR_OPEN)
		return -EINVAL;
	if (nfds > 0 && !ufds)
		return -EFAULT;
	if (usigmask && sigsetsize != sizeof(uint64_t))
		return -EINVAL;
	if (!usigmask && sigsetsize != 0 && sigsetsize != sizeof(uint64_t))
		return -EINVAL;

	if (utimeout) {
		if (copy_from_user(&timeout, utimeout, sizeof(timeout)) != 0)
			return -EFAULT;
		ret = sys_timespec_to_deadline(&timeout, &has_timeout,
					       &deadline);
		if (ret < 0)
			return ret;
	} else {
		ret = sys_timespec_to_deadline(NULL, &has_timeout, &deadline);
		if (ret < 0)
			return ret;
	}

	if (nfds > 0 && copy_from_user(fds, ufds, nfds * sizeof(fds[0])) != 0)
		return -EFAULT;

	if (usigmask) {
		uint64_t new_mask;

		if (copy_from_user(&new_mask, usigmask, sizeof(new_mask)) != 0)
			return -EFAULT;
		old_blocked = task_blocked_mask(current);
		task_set_blocked_mask(current, new_mask);
		swapped_mask = true;
	}

	scan_ctx.fds = fds;
	scan_ctx.nfds = nfds;
	ret = vfs_poll_wait_until(ppoll_scan, &scan_ctx, has_timeout, deadline);

	if (swapped_mask)
		task_set_blocked_mask(current, old_blocked);

	if (ret >= 0 && nfds > 0 &&
	    copy_to_user(ufds, fds, nfds * sizeof(fds[0])) != 0)
		return -EFAULT;

	return ret;
}

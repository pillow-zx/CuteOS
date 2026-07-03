/*
 * syscall/sys_file_poll.c - ppoll 系统调用
 *
 * 覆盖范围：
 *   超时时间戳转换（sys_timespec_to_deadline）、fd 就绪扫描（ppoll_scan）、
 *   以及带信号掩码的 ppoll 等待循环。
 */

#include <kernel/fdtable.h>
#include <kernel/bitops.h>
#include <kernel/cleanup.h>
#include <kernel/tools.h>
#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/types.h>
#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/vfs.h>
#include <asm/page.h>
#include <asm/trap.h>
#include <kernel/time.h>
#include <uapi/select.h>

struct sys_pollfd {
	int32_t fd;
	int16_t events;
	int16_t revents;
};

struct ppoll_scan_ctx {
	struct sys_pollfd *fds;
	size_t nfds;
};

struct pselect_scan_ctx {
	const fd_set *in_readfds;
	const fd_set *in_writefds;
	const fd_set *in_exceptfds;
	fd_set *out_readfds;
	fd_set *out_writefds;
	fd_set *out_exceptfds;
	size_t nfds;
};

struct poll_sigmask_guard {
	struct task_struct *task;
	uint64_t old_blocked;
	bool active;
};

CLEANUP_DEFINE(poll_sigmask_restore, struct poll_sigmask_guard,
	       if (_T.active)
		       task_set_blocked_mask(_T.task, _T.old_blocked);)

static_assert(NR_OPEN <= __FD_SETSIZE, "NR_OPEN exceeds fd_set ABI limit");

static __always_inline __must_check __pure size_t
sys_fdset_nwords(size_t nfds)
{
	if (nfds == 0)
		return 0;

	return (nfds + __NFDBITS - 1) / __NFDBITS;
}

static __always_inline __must_check __pure size_t
sys_fdset_nbytes(size_t nfds)
{
	return sys_fdset_nwords(nfds) * sizeof(unsigned long);
}

static __always_inline __must_check __pure bool
sys_fdset_test(const fd_set *set, int fd)
{
	return set && (set->fds_bits[fd / __NFDBITS] & BIT(fd % __NFDBITS));
}

static __always_inline void sys_fdset_assign(fd_set *set, int fd, bool ready)
{
	if (!set)
		return;

	if (ready)
		set->fds_bits[fd / __NFDBITS] |= BIT(fd % __NFDBITS);
	else
		set->fds_bits[fd / __NFDBITS] &= ~BIT(fd % __NFDBITS);
}

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

static int poll_apply_sigmask(const unsigned long *usigmask, size_t sigsetsize,
			      struct poll_sigmask_guard *guard)
{
	unsigned long new_mask;

	if (!guard)
		return -EINVAL;

	guard->task = current;
	guard->old_blocked = 0;
	guard->active = false;

	if (!usigmask) {
		if (sigsetsize != 0 && sigsetsize != sizeof(uint64_t))
			return -EINVAL;
		return 0;
	}
	if (sigsetsize != sizeof(uint64_t))
		return -EINVAL;
	if (copy_from_user(&new_mask, usigmask, sizeof(new_mask)) != 0)
		return -EFAULT;

	guard->old_blocked = task_blocked_mask(current);
	task_set_blocked_mask(current, new_mask);
	guard->active = true;
	return 0;
}

static int pselect_copy_sigmask_args(const struct pselect6_sigmask *upack,
				     const unsigned long **usigmask,
				     size_t *sigsetsize)
{
	struct pselect6_sigmask pack;

	*usigmask = NULL;
	*sigsetsize = 0;
	if (!upack)
		return 0;
	if (copy_from_user(&pack, upack, sizeof(pack)) != 0)
		return -EFAULT;

	*usigmask = pack.ss;
	*sigsetsize = (size_t)pack.ss_len;
	return 0;
}

static int pselect_copy_fdset(fd_set *dst, const fd_set *usrc, size_t bytes)
{
	memset(dst, 0, sizeof(*dst));
	if (!usrc || bytes == 0)
		return 0;
	if (copy_from_user(dst, usrc, bytes) != 0)
		return -EFAULT;

	return 0;
}

static int pselect_copy_result_fdset(fd_set *udst, const fd_set *src,
				     size_t bytes)
{
	if (!udst || bytes == 0)
		return 0;
	if (copy_to_user(udst, src, bytes) != 0)
		return -EFAULT;

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

static int pselect_scan(struct vfs_poll_table *table, void *arg)
{
	struct pselect_scan_ctx *ctx = arg;
	int ready = 0;

	for (size_t fd = 0; fd < ctx->nfds; fd++) {
		struct file *file;
		uint32_t events = 0;
		uint32_t mask;
		bool read_ready;
		bool write_ready;
		bool except_ready;

		if (sys_fdset_test(ctx->in_readfds, (int)fd))
			events |= POLLIN;
		if (sys_fdset_test(ctx->in_writefds, (int)fd))
			events |= POLLOUT;
		if (sys_fdset_test(ctx->in_exceptfds, (int)fd))
			events |= POLLPRI;
		if (!events)
			continue;

		file = fd_get((int)fd);
		if (!file)
			return -EBADF;

		mask = vfs_poll(file, events, table);
		file_put(file);

		read_ready = (mask & (POLLIN | POLLERR | POLLHUP)) != 0;
		write_ready = (mask & (POLLOUT | POLLERR)) != 0;
		except_ready = (mask & POLLPRI) != 0;

		sys_fdset_assign(ctx->out_readfds, (int)fd, read_ready);
		sys_fdset_assign(ctx->out_writefds, (int)fd, write_ready);
		sys_fdset_assign(ctx->out_exceptfds, (int)fd, except_ready);

		ready += read_ready;
		ready += write_ready;
		ready += except_ready;
	}

	return ready;
}

ssize_t sys_ppoll(struct trap_frame *tf)
{
	struct sys_pollfd *ufds = (struct sys_pollfd *)tf->a0;
	size_t nfds = (size_t)tf->a1;
	const struct timespec *utimeout =
		(const struct timespec *)tf->a2;
	const unsigned long *usigmask = (const unsigned long *)tf->a3;
	size_t sigsetsize = (size_t)tf->a4;
	struct sys_pollfd fds[NR_OPEN];
	struct timespec timeout;
	struct ppoll_scan_ctx scan_ctx;
	struct poll_sigmask_guard sigmask_guard
		__cleanup_with(poll_sigmask_restore) = {
			.task = current,
			.old_blocked = 0,
			.active = false,
		};
	bool has_timeout;
	uint64_t deadline;
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

	ret = poll_apply_sigmask(usigmask, sigsetsize, &sigmask_guard);
	if (ret < 0)
		return ret;

	scan_ctx.fds = fds;
	scan_ctx.nfds = nfds;
	ret = vfs_poll_wait_until(ppoll_scan, &scan_ctx, has_timeout, deadline);

	if (ret >= 0 && nfds > 0 &&
	    copy_to_user(ufds, fds, nfds * sizeof(fds[0])) != 0)
		return -EFAULT;

	return ret;
}

ssize_t sys_pselect6(struct trap_frame *tf)
{
	long nfds = (long)tf->a0;
	fd_set *ureadfds = (fd_set *)tf->a1;
	fd_set *uwritefds = (fd_set *)tf->a2;
	fd_set *uexceptfds = (fd_set *)tf->a3;
	const struct timespec *utimeout = (const struct timespec *)tf->a4;
	const struct pselect6_sigmask *usigpack =
		(const struct pselect6_sigmask *)tf->a5;
	const unsigned long *usigmask;
	fd_set in_readfds;
	fd_set in_writefds;
	fd_set in_exceptfds;
	fd_set out_readfds;
	fd_set out_writefds;
	fd_set out_exceptfds;
	struct timespec timeout;
	struct pselect_scan_ctx scan_ctx;
	struct poll_sigmask_guard sigmask_guard
		__cleanup_with(poll_sigmask_restore) = {
			.task = current,
			.old_blocked = 0,
			.active = false,
		};
	bool has_timeout;
	uint64_t deadline;
	size_t sigsetsize;
	size_t fdset_bytes;
	int ready;
	int ret;

	if (nfds < 0 || nfds > NR_OPEN)
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

	fdset_bytes = sys_fdset_nbytes((size_t)nfds);
	ret = pselect_copy_fdset(&in_readfds, ureadfds, fdset_bytes);
	if (ret < 0)
		return ret;
	ret = pselect_copy_fdset(&in_writefds, uwritefds, fdset_bytes);
	if (ret < 0)
		return ret;
	ret = pselect_copy_fdset(&in_exceptfds, uexceptfds, fdset_bytes);
	if (ret < 0)
		return ret;

	memset(&out_readfds, 0, sizeof(out_readfds));
	memset(&out_writefds, 0, sizeof(out_writefds));
	memset(&out_exceptfds, 0, sizeof(out_exceptfds));

	ret = pselect_copy_sigmask_args(usigpack, &usigmask, &sigsetsize);
	if (ret < 0)
		return ret;
	ret = poll_apply_sigmask(usigmask, sigsetsize, &sigmask_guard);
	if (ret < 0)
		return ret;

	scan_ctx.in_readfds = ureadfds ? &in_readfds : NULL;
	scan_ctx.in_writefds = uwritefds ? &in_writefds : NULL;
	scan_ctx.in_exceptfds = uexceptfds ? &in_exceptfds : NULL;
	scan_ctx.out_readfds = ureadfds ? &out_readfds : NULL;
	scan_ctx.out_writefds = uwritefds ? &out_writefds : NULL;
	scan_ctx.out_exceptfds = uexceptfds ? &out_exceptfds : NULL;
	scan_ctx.nfds = (size_t)nfds;

	ready = vfs_poll_wait_until(pselect_scan, &scan_ctx, has_timeout,
				    deadline);
	if (ready < 0)
		return ready;

	ret = pselect_copy_result_fdset(ureadfds, &out_readfds, fdset_bytes);
	if (ret < 0)
		return ret;
	ret = pselect_copy_result_fdset(uwritefds, &out_writefds, fdset_bytes);
	if (ret < 0)
		return ret;
	ret = pselect_copy_result_fdset(uexceptfds, &out_exceptfds,
					 fdset_bytes);
	if (ret < 0)
		return ret;

	return ready;
}

/*
 * syscall/sys_file_poll.c - poll/select/epoll 系统调用
 */

#include <kernel/fdtable.h>
#include <kernel/cleanup.h>
#include <kernel/tools.h>
#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/slab.h>
#include <kernel/signal.h>
#include <kernel/types.h>
#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/vfs.h>
#include <kernel/page.h>
#include <kernel/trap.h>
#include <kernel/time.h>
#include <uapi/eventpoll.h>
#include <uapi/poll.h>
#include <uapi/select.h>

struct ppoll_scan_ctx {
	struct pollfd *fds;
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

struct epoll_scan_ctx {
	struct eventpoll *ep;
	struct epoll_event *events;
	size_t maxevents;
	size_t nr_ready;
};

struct poll_sigmask_guard {
	struct task_struct *task;
	uint64_t old_blocked;
	bool active;
};

struct epitem {
	struct list_head node;
	struct file *file;
	struct epoll_event event;
	int fd;
};

struct eventpoll {
	struct list_head items;
	struct wait_queue_head waitq;
};

CLEANUP_DEFINE(poll_sigmask_restore, struct poll_sigmask_guard,
	       if (_T.active) task_set_blocked_mask(_T.task, _T.old_blocked);)
CLEANUP_DEFINE(
	epitem, struct epitem *, if (_T) {
		file_put(_T->file);
		kfree(_T);
	});
CLEANUP_DEFINE(eventpoll, struct eventpoll *, if (_T) kfree(_T));

static_assert(NR_OPEN <= __FD_SETSIZE, "NR_OPEN exceeds fd_set ABI limit");
static_assert(EPOLL_CLOEXEC == O_CLOEXEC, "epoll cloexec flag ABI mismatch");
static_assert(sizeof(struct pollfd) == 8, "pollfd ABI layout mismatch");
static_assert(sizeof(struct epoll_event) == 16,
	      "epoll_event ABI layout mismatch");
static_assert(offsetof(struct epoll_event, data) == 8,
	      "epoll_event data ABI offset mismatch");

static int eventpoll_release(struct file *file);
static uint32_t eventpoll_poll(struct file *file, uint32_t events,
			       struct vfs_poll_table *table);

static const struct file_operations eventpoll_fops = {
	.poll = eventpoll_poll,
	.release = eventpoll_release,
};

static __always_inline __must_check __pure bool
eventpoll_file(struct file *file)
{
	return file && file->f_op == &eventpoll_fops;
}

static __always_inline __must_check __pure size_t sys_fdset_nwords(size_t nfds)
{
	if (nfds == 0)
		return 0;

	return (nfds + __NFDBITS - 1) / __NFDBITS;
}

static __always_inline __must_check __pure size_t sys_fdset_nbytes(size_t nfds)
{
	return sys_fdset_nwords(nfds) * sizeof(unsigned long);
}

static __always_inline __must_check __pure bool
sys_epoll_create1_flags_ok(int flags)
{
	return (flags & ~EPOLL_CLOEXEC) == 0;
}

static __always_inline __must_check __pure bool sys_epoll_op_valid(int op)
{
	return op == EPOLL_CTL_ADD || op == EPOLL_CTL_MOD ||
	       op == EPOLL_CTL_DEL;
}

static __always_inline __must_check __pure bool
sys_epoll_events_ok(uint32_t events)
{
	const uint32_t supported = EPOLLIN | EPOLLOUT | EPOLLPRI | EPOLLERR |
				   EPOLLHUP | EPOLLRDNORM | EPOLLRDBAND |
				   EPOLLWRNORM | EPOLLWRBAND | EPOLLMSG |
				   EPOLLRDHUP | EPOLLONESHOT | EPOLLET;

	return (events & ~supported) == 0;
}

static __always_inline __must_check __pure bool
sys_epoll_wait_sigmask_ok(const unsigned long *usigmask, size_t sigsetsize)
{
	if (usigmask)
		return sigsetsize == sizeof(unsigned long);

	return sigsetsize == 0 || sigsetsize == sizeof(unsigned long);
}

static __always_inline __must_check __pure bool
sys_fdset_test(const fd_set *set, int fd)
{
	return set && FD_ISSET(fd, set);
}

static __always_inline void sys_fdset_assign(fd_set *set, int fd, bool ready)
{
	if (!set)
		return;

	if (ready)
		FD_SET(fd, set);
	else
		FD_CLR(fd, set);
}

static __always_inline __must_check __pure uint32_t poll_req(uint32_t events)
{
	uint32_t req = events;

	if (events & (POLLRDNORM | POLLRDBAND))
		req |= POLLIN;
	if (events & (POLLWRNORM | POLLWRBAND))
		req |= POLLOUT;
	if (events & POLLRDHUP)
		req |= POLLIN;

	return req;
}

static __always_inline __must_check __pure uint32_t poll_res(uint32_t mask,
							     uint32_t requested)
{
	uint32_t res = mask;

	if ((requested & POLLRDNORM) && (mask & POLLIN))
		res |= POLLRDNORM;
	if ((requested & POLLRDBAND) && (mask & POLLIN))
		res |= POLLRDBAND;
	if ((requested & POLLWRNORM) && (mask & POLLOUT))
		res |= POLLWRNORM;
	if ((requested & POLLWRBAND) && (mask & POLLOUT))
		res |= POLLWRBAND;

	return res;
}

static __always_inline __must_check __pure uint32_t
epoll_res(uint32_t events, uint32_t requested)
{
	uint32_t res = events;

	if ((requested & EPOLLRDNORM) && (events & EPOLLIN))
		res |= EPOLLRDNORM;
	if ((requested & EPOLLRDBAND) && (events & EPOLLIN))
		res |= EPOLLRDBAND;
	if ((requested & EPOLLWRNORM) && (events & EPOLLOUT))
		res |= EPOLLWRNORM;
	if ((requested & EPOLLWRBAND) && (events & EPOLLOUT))
		res |= EPOLLWRBAND;

	return res;
}

static int poll_apply_sigmask(const unsigned long *usigmask, size_t sigsetsize,
			      struct poll_sigmask_guard *guard)
{
	unsigned long new_mask;

	if (!guard)
		return -EINVAL;

	guard->task = current_task();
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

	guard->old_blocked = task_blocked_mask(current_task());
	task_set_blocked_mask(current_task(), new_mask);
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

		mask = vfs_poll(file, poll_req((uint32_t)ctx->fds[i].events),
				table);
		mask = poll_res(mask, (uint32_t)ctx->fds[i].events);
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

static struct epitem *eventpoll_find(struct eventpoll *ep, int fd,
				     struct file *file)
{
	struct epitem *item;

	if (!ep || !file)
		return NULL;

	list_for_each_entry (item, &ep->items, node) {
		if (item->fd == fd && item->file == file)
			return item;
	}

	return NULL;
}

static __always_inline __must_check __pure bool
epitem_trigger_supported(const struct epitem *item)
{
	return item && (item->event.events & (EPOLLET | EPOLLONESHOT)) == 0;
}

static __always_inline __must_check __pure uint32_t
epitem_poll_events(const struct epitem *item)
{
	uint32_t events = 0;

	if (!item || !item->file)
		return 0;
	if (item->file->f_mode & FMODE_READ)
		events |= POLLIN;
	if (item->file->f_mode & FMODE_WRITE)
		events |= POLLOUT;
	if (item->event.events & EPOLLPRI)
		events |= POLLPRI;

	if (item->event.events & (EPOLLRDNORM | EPOLLRDBAND | EPOLLRDHUP))
		events |= POLLIN;
	if (item->event.events & (EPOLLWRNORM | EPOLLWRBAND))
		events |= POLLOUT;

	return events;
}

static __always_inline __must_check __pure uint32_t
epoll_result_events(const struct epitem *item, uint32_t mask)
{
	uint32_t events = 0;
	uint32_t wanted;

	if (!item)
		return 0;
	if (mask & POLLIN)
		events |= EPOLLIN;
	if (mask & POLLOUT)
		events |= EPOLLOUT;
	if (mask & POLLPRI)
		events |= EPOLLPRI;
	if (mask & POLLERR)
		events |= EPOLLERR;
	if (mask & POLLHUP)
		events |= EPOLLHUP;
	if (mask & POLLNVAL)
		events |= EPOLLNVAL;

	events = epoll_res(events, item->event.events);
	wanted = item->event.events &
		 (EPOLLIN | EPOLLOUT | EPOLLPRI | EPOLLRDNORM | EPOLLRDBAND |
		  EPOLLWRNORM | EPOLLWRBAND);
	return events & (wanted | EPOLLERR | EPOLLHUP | EPOLLNVAL);
}

static int epoll_scan(struct vfs_poll_table *table, void *arg)
{
	struct epoll_scan_ctx *ctx = arg;
	struct epitem *item;

	if (!ctx || !ctx->ep)
		return -EINVAL;

	ctx->nr_ready = 0;
	if (table)
		vfs_poll_wait(table, &ctx->ep->waitq);

	list_for_each_entry (item, &ctx->ep->items, node) {
		uint32_t mask;
		uint32_t events;

		if (!epitem_trigger_supported(item))
			return -EINVAL;

		mask = vfs_poll(item->file, epitem_poll_events(item), table);
		events = epoll_result_events(item, mask);
		if (!events)
			continue;
		if (ctx->nr_ready >= ctx->maxevents)
			continue;

		ctx->events[ctx->nr_ready].events = events;
		ctx->events[ctx->nr_ready].data = item->event.data;
		ctx->nr_ready++;
	}

	return (int)ctx->nr_ready;
}

static uint32_t eventpoll_poll(struct file *file, uint32_t events,
			       struct vfs_poll_table *table)
{
	struct eventpoll *ep = file ? file->private_data : NULL;

	if (!ep)
		return POLLERR;

	if (events & (POLLIN | POLLOUT))
		vfs_poll_wait(table, &ep->waitq);

	return 0;
}

static int eventpoll_release(struct file *file)
{
	struct eventpoll *ep;
	struct list_head *pos;
	struct list_head *next;

	if (!file)
		return 0;

	ep = file->private_data;
	file->private_data = NULL;
	if (!ep)
		return 0;

	list_for_each_safe (pos, next, &ep->items) {
		struct epitem *item = list_entry(pos, struct epitem, node);

		list_del_init(&item->node);
		file_put(item->file);
		kfree(item);
	}
	kfree(ep);
	return 0;
}

/*
 * SYSCALL_SUPPORT(B): epoll_create1
 * Current: creates an eventpoll file and supports EPOLL_CLOEXEC.
 * Unsupported errno: unknown flags return -EINVAL.
 * Future: add nested, close, and epoll-fd readiness coverage.
 */
ssize_t sys_epoll_create1(struct trap_frame *tf)
{
	int flags = (int)syscall_arg(tf, 0);
	struct eventpoll *ep __cleanup_with(eventpoll) = NULL;
	struct file *file __cleanup_with(file) = NULL;
	int fd;

	if (!sys_epoll_create1_flags_ok(flags))
		return -EINVAL;

	ep = kmalloc(sizeof(*ep));
	if (!ep)
		return -ENOMEM;

	INIT_LIST_HEAD(&ep->items);
	init_waitqueue_head(&ep->waitq);

	file = file_alloc(&eventpoll_fops, FMODE_READ | FMODE_WRITE, ep);
	if (!file)
		return -ENOMEM;

	fd = fd_alloc_flags(file, flags);
	if (fd < 0)
		return fd;

	cleanup_forget_ptr(file);
	cleanup_forget_ptr(ep);
	return fd;
}

/*
 * SYSCALL_SUPPORT(B): epoll_ctl
 * Current: supports ADD, MOD, and DEL for poll-capable non-epoll fds.
 * Unsupported errno: invalid ops, targets, or event bits return -EINVAL;
 * non-pollable fds return -EPERM.
 * Future: choose whether EPOLLET and EPOLLONESHOT are rejected or implemented.
 */
ssize_t sys_epoll_ctl(struct trap_frame *tf)
{
	int epfd = (int)syscall_arg(tf, 0);
	int op = (int)syscall_arg(tf, 1);
	int fd = (int)syscall_arg(tf, 2);
	const struct epoll_event *uevent = (const struct epoll_event *)syscall_arg(tf, 3);
	struct file *epfile __cleanup_with(file) = NULL;
	struct file *file __cleanup_with(file) = NULL;
	struct epitem *item __cleanup_with(epitem) = NULL;
	struct epitem *found;
	struct epoll_event event;
	struct eventpoll *ep;

	if (!sys_epoll_op_valid(op))
		return -EINVAL;

	epfile = fd_get(epfd);
	if (!epfile)
		return -EBADF;
	if (!eventpoll_file(epfile))
		return -EINVAL;
	if (fd == epfd)
		return -EINVAL;

	file = fd_get(fd);
	if (!file)
		return -EBADF;
	if (eventpoll_file(file))
		return -EINVAL;
	if (!file->f_op || !file->f_op->poll)
		return -EPERM;

	ep = epfile->private_data;
	if (!ep)
		return -EINVAL;

	switch (op) {
	case EPOLL_CTL_ADD:
		if (!uevent)
			return -EFAULT;
		if (copy_from_user(&event, uevent, sizeof(event)) != 0)
			return -EFAULT;
		if (!sys_epoll_events_ok(event.events))
			return -EINVAL;
		if (eventpoll_find(ep, fd, file))
			return -EEXIST;

		item = kmalloc(sizeof(*item));
		if (!item)
			return -ENOMEM;

		INIT_LIST_HEAD(&item->node);
		item->file = file;
		item->event = event;
		item->fd = fd;
		list_add_tail(&item->node, &ep->items);
		wake_up_all(&ep->waitq);

		cleanup_forget_ptr(file);
		cleanup_forget_ptr(item);
		return 0;

	case EPOLL_CTL_MOD:
		if (!uevent)
			return -EFAULT;
		if (copy_from_user(&event, uevent, sizeof(event)) != 0)
			return -EFAULT;
		if (!sys_epoll_events_ok(event.events))
			return -EINVAL;

		found = eventpoll_find(ep, fd, file);
		if (!found)
			return -ENOENT;
		found->event = event;
		wake_up_all(&ep->waitq);
		return 0;

	case EPOLL_CTL_DEL:
		found = eventpoll_find(ep, fd, file);
		if (!found)
			return -ENOENT;

		list_del_init(&found->node);
		file_put(found->file);
		kfree(found);
		wake_up_all(&ep->waitq);
		return 0;
	}

	return -EINVAL;
}

/*
 * SYSCALL_SUPPORT(B): epoll_pwait
 * Current: waits on registered level-triggered items with optional sigmask.
 * Unsupported errno: bad sigset size or maxevents returns -EINVAL; registered
 * EPOLLET/EPOLLONESHOT items currently make scanning return -EINVAL.
 * Future: fix edge/oneshot policy and signal interruption details.
 */
ssize_t sys_epoll_pwait(struct trap_frame *tf)
{
	int epfd = (int)syscall_arg(tf, 0);
	struct epoll_event *uevents = (struct epoll_event *)syscall_arg(tf, 1);
	int maxevents = (int)syscall_arg(tf, 2);
	long timeout = (long)syscall_arg(tf, 3);
	const unsigned long *usigmask = (const unsigned long *)syscall_arg(tf, 4);
	size_t sigsetsize = (size_t)syscall_arg(tf, 5);
	struct epoll_event kevents[NR_OPEN];
	struct file *epfile __cleanup_with(file) = NULL;
	struct poll_sigmask_guard sigmask_guard __cleanup_with(
		poll_sigmask_restore) = {
		.task = current_task(),
		.old_blocked = 0,
		.active = false,
	};
	struct epoll_scan_ctx scan_ctx;
	struct eventpoll *ep;
	bool has_timeout;
	uint64_t deadline;
	size_t scan_limit;
	int ret;

	if (maxevents <= 0)
		return -EINVAL;
	if (!uevents)
		return -EFAULT;
	if (!sys_epoll_wait_sigmask_ok(usigmask, sigsetsize))
		return -EINVAL;

	epfile = fd_get(epfd);
	if (!epfile)
		return -EBADF;
	if (!eventpoll_file(epfile))
		return -EINVAL;

	ret = mtime_deadline_from_ms(timeout, &has_timeout, &deadline);
	if (ret < 0)
		return ret;

	ret = poll_apply_sigmask(usigmask, sigsetsize, &sigmask_guard);
	if (ret < 0)
		return ret;

	ep = epfile->private_data;
	if (!ep)
		return -EINVAL;

	scan_limit = (size_t)maxevents;
	if (scan_limit > ARRLEN(kevents))
		scan_limit = ARRLEN(kevents);

	scan_ctx.ep = ep;
	scan_ctx.events = kevents;
	scan_ctx.maxevents = scan_limit;
	scan_ctx.nr_ready = 0;

	ret = vfs_poll_wait_until(epoll_scan, &scan_ctx, has_timeout, deadline);
	if (ret > 0 && copy_to_user(uevents, kevents,
				    (size_t)ret * sizeof(kevents[0])) != 0)
		return -EFAULT;

	return ret;
}

/*
 * SYSCALL_SUPPORT(B): ppoll
 * Current: scans pollfd entries with timeout and optional temporary sigmask.
 * Unsupported errno: nfds above NR_OPEN or invalid sigset size returns
 * -EINVAL; signal race semantics are simplified.
 * Future: document NR_OPEN limits and add signal interruption coverage.
 */
ssize_t sys_ppoll(struct trap_frame *tf)
{
	struct pollfd *ufds = (struct pollfd *)syscall_arg(tf, 0);
	size_t nfds = (size_t)syscall_arg(tf, 1);
	const struct timespec *utimeout = (const struct timespec *)syscall_arg(tf, 2);
	const unsigned long *usigmask = (const unsigned long *)syscall_arg(tf, 3);
	size_t sigsetsize = (size_t)syscall_arg(tf, 4);
	struct pollfd fds[NR_OPEN];
	struct timespec timeout;
	struct ppoll_scan_ctx scan_ctx;
	struct poll_sigmask_guard sigmask_guard __cleanup_with(
		poll_sigmask_restore) = {
		.task = current_task(),
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
		ret = mtime_deadline_from_timespec(&timeout, &has_timeout,
						   &deadline);
		if (ret < 0)
			return ret;
	} else {
		ret = mtime_deadline_from_timespec(NULL, &has_timeout,
						   &deadline);
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

/*
 * SYSCALL_SUPPORT(B): pselect6
 * Current: scans fd_sets with timeout and optional packed sigmask.
 * Unsupported errno: nfds outside [0, NR_OPEN] or invalid sigmask metadata
 * returns -EINVAL; signal race semantics are simplified.
 * Future: add signal interruption and race-behavior coverage.
 */
ssize_t sys_pselect6(struct trap_frame *tf)
{
	long nfds = (long)syscall_arg(tf, 0);
	fd_set *ureadfds = (fd_set *)syscall_arg(tf, 1);
	fd_set *uwritefds = (fd_set *)syscall_arg(tf, 2);
	fd_set *uexceptfds = (fd_set *)syscall_arg(tf, 3);
	const struct timespec *utimeout = (const struct timespec *)syscall_arg(tf, 4);
	const struct pselect6_sigmask *usigpack =
		(const struct pselect6_sigmask *)syscall_arg(tf, 5);
	const unsigned long *usigmask;
	fd_set in_readfds;
	fd_set in_writefds;
	fd_set in_exceptfds;
	fd_set out_readfds;
	fd_set out_writefds;
	fd_set out_exceptfds;
	struct timespec timeout;
	struct pselect_scan_ctx scan_ctx;
	struct poll_sigmask_guard sigmask_guard __cleanup_with(
		poll_sigmask_restore) = {
		.task = current_task(),
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
		ret = mtime_deadline_from_timespec(&timeout, &has_timeout,
						   &deadline);
		if (ret < 0)
			return ret;
	} else {
		ret = mtime_deadline_from_timespec(NULL, &has_timeout,
						   &deadline);
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

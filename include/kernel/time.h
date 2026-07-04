#ifndef _CUTEOS_KERNEL_TIME_H
#define _CUTEOS_KERNEL_TIME_H

#include <kernel/compiler.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>
#include <uapi/time.h>

struct ktimer;
struct task_struct;

typedef void (*ktimer_fn_t)(struct ktimer *timer, void *arg);

struct ktimer {
	struct list_head node;
	ktimer_fn_t function;
	void *arg;
	uint64_t expires;
	uint64_t interval;
	bool active;
};

#define ITIMER_COUNT 3

struct itimer_state {
	spinlock_t lock;
	struct itimerval value;
	struct ktimer timer;
	struct task_struct *target;
};

static __always_inline __must_check __pure bool clock_id_supported(int clock_id)
{
	return clock_id == CLOCK_REALTIME || clock_id == CLOCK_MONOTONIC ||
	       clock_id == CLOCK_BOOTTIME;
}

static __always_inline __must_check __pure bool itimer_which_valid(int which)
{
	return which == ITIMER_REAL || which == ITIMER_VIRTUAL ||
	       which == ITIMER_PROF;
}

static __always_inline __must_check __pure size_t itimer_which_index(int which)
{
	return (size_t)which;
}

static __always_inline __nonnull(1, 2) __access_no_size(read_only, 1)
	__access_no_size(write_only, 2) void
	itimer_state_value(const struct itimer_state *state,
			   struct itimerval *value)
{
	*value = state->value;
}

static __always_inline __must_check __pure __nonnull(1)
__access_no_size(read_only, 1) bool ktimer_active(const struct ktimer *timer)
{
	return timer->active;
}

static __always_inline __must_check __pure __nonnull(1)
__access_no_size(read_only, 1) bool ktimer_expired(const struct ktimer *timer,
						   uint64_t now)
{
	return timer->active && timer->expires <= now;
}

static __always_inline __must_check __pure __nonnull(1)
	__access_no_size(read_only, 1) uint64_t
	ktimer_remaining(const struct ktimer *timer, uint64_t now)
{
	if (!timer->active || timer->expires <= now)
		return 0;
	return timer->expires - now;
}

void __nonnull(2) __access_no_size(write_only, 2)
	mtime_to_timespec(uint64_t ticks, struct timespec *ts);
int __must_check __access_no_size(read_only, 1)
	__access_no_size(write_only, 2)
	timespec_to_mtime_delta(const struct timespec *ts, uint64_t *delta);
uint64_t __must_check __const mtime_deadline_after(uint64_t now,
						  uint64_t delta);
int __must_check __nonnull(2, 3) __access_no_size(write_only, 2)
	__access_no_size(write_only, 3)
	mtime_deadline_from_timespec(const struct timespec *ts,
				     bool *has_timeout, uint64_t *deadline);
int __must_check __nonnull(2, 3) __access_no_size(write_only, 2)
	__access_no_size(write_only, 3)
	mtime_deadline_from_ms(long timeout_ms, bool *has_timeout,
			       uint64_t *deadline);

void __nonnull(1) __access_no_size(write_only, 1)
	ktimer_init(struct ktimer *timer, ktimer_fn_t function, void *arg);
int __must_check __nonnull(1) __access_no_size(read_write, 1)
	ktimer_arm(struct ktimer *timer, uint64_t expires, uint64_t interval);
bool __must_check __nonnull(1) __access_no_size(read_write, 1)
	ktimer_cancel(struct ktimer *timer);

void ktimer_run_expired(uint64_t now);

void __nonnull(1) __access_no_size(write_only, 1)
	itimer_state_init(struct itimer_state *state);
void __nonnull(1) __access_no_size(read_write, 1)
	itimer_state_destroy(struct itimer_state *state);
int __must_check __nonnull(1, 2) __access_no_size(read_write, 1)
	__access_no_size(write_only, 2)
	itimer_get_value(struct itimer_state *state, struct itimerval *value);
int __must_check __nonnull(1, 2, 3) __access_no_size(read_write, 1)
	__access_no_size(read_only, 3)
	itimer_set_real(struct itimer_state *state, struct task_struct *target,
			const struct itimerval *new_value,
			struct itimerval *old_value);

#endif

#ifndef _CUTEOS_KERNEL_TIME_H
#define _CUTEOS_KERNEL_TIME_H

#include <kernel/compiler.h>
#include <kernel/list.h>
#include <kernel/types.h>
#include <uapi/time.h>

struct ktimer;

typedef void (*ktimer_fn_t)(struct ktimer *timer, void *arg);

struct ktimer {
	struct list_head node;
	ktimer_fn_t function;
	void *arg;
	uint64_t expires;
	uint64_t interval;
	bool active;
};

static __always_inline __must_check __pure bool clock_id_supported(int clock_id)
{
	return clock_id == CLOCK_REALTIME || clock_id == CLOCK_MONOTONIC ||
	       clock_id == CLOCK_BOOTTIME;
}

static __always_inline __must_check __pure
__access_no_size(read_only, 1) bool ktimer_active(const struct ktimer *timer)
{
	return timer->active;
}

static __always_inline __must_check __pure __nonnull(1)
	__access_no_size(read_only, 1) uint64_t
	ktimer_remaining(const struct ktimer *timer, uint64_t now)
{
	if (!timer->active || timer->expires <= now)
		return 0;
	return timer->expires - now;
}

void mtime_to_timespec(uint64_t ticks, struct timespec *ts);
int timespec_to_mtime_delta(const struct timespec *ts, uint64_t *delta);
uint64_t mtime_deadline_after(uint64_t now, uint64_t delta);

void __nonnull(1) __access_no_size(write_only, 1)
	ktimer_init(struct ktimer *timer, ktimer_fn_t function, void *arg);
int __must_check __nonnull(1) __access_no_size(read_write, 1)
	ktimer_arm(struct ktimer *timer, uint64_t expires, uint64_t interval);
bool __must_check __nonnull(1) __access_no_size(read_write, 1)
	ktimer_cancel(struct ktimer *timer);

void ktimer_run_expired(uint64_t now);

#endif

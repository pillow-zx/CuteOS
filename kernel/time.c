/*
 * kernel/time.c - 时间子系统核心实现
 *
 * 功能：
 *   提供内核内部时间工具函数，包括 mtime tick 与 timespec/timeval
 *   之间的转换。本文件仅包含内部 API；ABI 边界处理（sys_* 函数）
 *   位于 syscall/sys_time.c。
 *
 * 内部函数：
 *   clock_id_supported(clock_id) - 验证 clock_id 是否受支持
 *   mtime_to_timespec(ticks, ts) - mtime tick → struct timespec
 *   timespec_to_mtime_delta(ts, delta) - struct timespec → tick 增量
 *   mtime_deadline_after(now, delta) - 带饱和的截止时间计算
 */

#include <kernel/errno.h>
#include <kernel/list.h>
#include <kernel/signal.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/timer.h>
#include <kernel/types.h>

#define USEC_PER_SEC 1000000UL

static struct {
	spinlock_t lock;
	struct list_head entries;
} ktimer_queue = {
	.lock = SPINLOCK_INIT,
	.entries = LIST_HEAD_INIT(ktimer_queue.entries),
};

static uint64_t nsec_from_mtime_remainder(uint64_t ticks)
{
	return ticks * 1000000000UL / MTIME_FREQ;
}

static bool timeval_is_zero(const struct timeval *tv)
{
	return tv->tv_sec == 0 && tv->tv_usec == 0;
}

static bool itimerval_value_is_zero(const struct itimerval *value)
{
	return timeval_is_zero(&value->it_value);
}

static int timeval_to_mtime_delta(const struct timeval *tv, uint64_t *delta)
{
	uint64_t sec_ticks;
	uint64_t usec_ticks;

	if (!tv || !delta)
		return -EINVAL;
	if (tv->tv_sec < 0 || tv->tv_usec < 0 ||
	    tv->tv_usec >= (long)USEC_PER_SEC)
		return -EINVAL;

	if ((uint64_t)tv->tv_sec > UINT64_MAX / MTIME_FREQ)
		sec_ticks = UINT64_MAX;
	else
		sec_ticks = (uint64_t)tv->tv_sec * MTIME_FREQ;

	usec_ticks = ((uint64_t)tv->tv_usec * MTIME_FREQ +
		      USEC_PER_SEC - 1) /
		     USEC_PER_SEC;
	if (usec_ticks > UINT64_MAX - sec_ticks)
		*delta = UINT64_MAX;
	else
		*delta = sec_ticks + usec_ticks;

	return 0;
}

static void mtime_to_timeval(uint64_t ticks, struct timeval *tv)
{
	uint64_t sec = ticks / MTIME_FREQ;
	uint64_t rem = ticks % MTIME_FREQ;
	uint64_t usec;

	usec = (rem * USEC_PER_SEC + MTIME_FREQ - 1) / MTIME_FREQ;
	if (usec >= USEC_PER_SEC) {
		sec++;
		usec -= USEC_PER_SEC;
	}

	tv->tv_sec = (long)sec;
	tv->tv_usec = (long)usec;
}

static void ktimer_insert_locked(struct ktimer *timer)
{
	struct list_head *pos;

	list_for_each (pos, &ktimer_queue.entries) {
		struct ktimer *queued = list_entry(pos, struct ktimer, node);

		if (timer->expires < queued->expires) {
			__list_add(&timer->node, pos->prev, pos);
			return;
		}
	}

	list_add_tail(&timer->node, &ktimer_queue.entries);
}

static uint64_t ktimer_next_interval_deadline(uint64_t expires,
					      uint64_t interval, uint64_t now)
{
	uint64_t next = mtime_deadline_after(expires, interval);

	while (next <= now && next != UINT64_MAX)
		next = mtime_deadline_after(next, interval);
	return next;
}

static struct ktimer *ktimer_detach_first_expired_locked(uint64_t now)
{
	struct ktimer *timer;

	if (list_empty(&ktimer_queue.entries))
		return NULL;

	timer = list_first_entry(&ktimer_queue.entries, struct ktimer, node);
	if (timer->expires > now)
		return NULL;

	list_del_init(&timer->node);
	timer->active = false;
	return timer;
}

static void itimer_real_fire(struct ktimer *timer, void *arg)
{
	struct itimer_state *state =
		container_of(timer, struct itimer_state, timer);
	struct task_struct *target;
	irq_flags_t flags;
	bool active;

	(void)arg;

	spin_lock_irqsave(&state->lock, &flags);
	target = state->target;
	active = ktimer_active(timer);
	if (!active) {
		state->value = (struct itimerval){0};
		state->target = NULL;
	}
	spin_unlock_irqrestore(&state->lock, flags);

	if (target)
		(void)send_signal(SIGALRM, target);
}

static void itimer_snapshot_locked(struct itimer_state *state,
				   struct itimerval *value, uint64_t now)
{
	itimer_state_value(state, value);
	if (ktimer_active(&state->timer))
		mtime_to_timeval(ktimer_remaining(&state->timer, now),
				 &value->it_value);
	else
		value->it_value = (struct timeval){0};
}

void mtime_to_timespec(uint64_t ticks, struct timespec *ts)
{
	uint64_t sec = ticks / MTIME_FREQ;
	uint64_t rem = ticks % MTIME_FREQ;

	ts->tv_sec = (int64_t)sec;
	ts->tv_nsec = (int64_t)nsec_from_mtime_remainder(rem);
}

int timespec_to_mtime_delta(const struct timespec *ts, uint64_t *delta)
{
	uint64_t sec_ticks;
	uint64_t nsec_ticks;

	if (!ts || !delta)
		return -EINVAL;
	if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000LL)
		return -EINVAL;

	if ((uint64_t)ts->tv_sec > UINT64_MAX / MTIME_FREQ)
		sec_ticks = UINT64_MAX;
	else
		sec_ticks = (uint64_t)ts->tv_sec * MTIME_FREQ;

	nsec_ticks = ((uint64_t)ts->tv_nsec * MTIME_FREQ + 999999999ULL) /
		     1000000000ULL;
	if (nsec_ticks > UINT64_MAX - sec_ticks)
		*delta = UINT64_MAX;
	else
		*delta = sec_ticks + nsec_ticks;

	return 0;
}

uint64_t mtime_deadline_after(uint64_t now, uint64_t delta)
{
	if (delta > UINT64_MAX - now)
		return UINT64_MAX;
	return now + delta;
}

int mtime_deadline_from_timespec(const struct timespec *ts, bool *has_timeout,
				 uint64_t *deadline)
{
	uint64_t delta;
	int ret;

	*has_timeout = false;
	*deadline = 0;
	if (!ts)
		return 0;

	ret = timespec_to_mtime_delta(ts, &delta);
	if (ret < 0)
		return ret;

	*has_timeout = true;
	*deadline = mtime_deadline_after(arch_timer_now(), delta);
	return 0;
}

int mtime_deadline_from_ms(long timeout_ms, bool *has_timeout,
			   uint64_t *deadline)
{
	uint64_t delta;
	uint64_t ms;

	*has_timeout = false;
	*deadline = 0;
	if (timeout_ms < 0)
		return 0;

	*has_timeout = true;
	if (timeout_ms == 0) {
		*deadline = arch_timer_now();
		return 0;
	}

	ms = (uint64_t)timeout_ms;
	if (ms > (UINT64_MAX - 999ULL) / MTIME_FREQ)
		delta = UINT64_MAX;
	else
		delta = (ms * MTIME_FREQ + 999ULL) / 1000ULL;

	*deadline = mtime_deadline_after(arch_timer_now(), delta);
	return 0;
}

void ktimer_init(struct ktimer *timer, ktimer_fn_t function, void *arg)
{
	INIT_LIST_HEAD(&timer->node);
	timer->function = function;
	timer->arg = arg;
	timer->expires = 0;
	timer->interval = 0;
	timer->active = false;
}

int ktimer_arm(struct ktimer *timer, uint64_t expires, uint64_t interval)
{
	irq_flags_t flags;

	spin_lock_irqsave(&ktimer_queue.lock, &flags);
	if (timer->active)
		list_del_init(&timer->node);
	timer->expires = expires;
	timer->interval = interval;
	timer->active = true;
	ktimer_insert_locked(timer);
	spin_unlock_irqrestore(&ktimer_queue.lock, flags);
	return 0;
}

bool ktimer_cancel(struct ktimer *timer)
{
	irq_flags_t flags;
	bool active;

	spin_lock_irqsave(&ktimer_queue.lock, &flags);
	active = timer->active;
	if (timer->active) {
		list_del_init(&timer->node);
		timer->active = false;
	}
	spin_unlock_irqrestore(&ktimer_queue.lock, flags);
	return active;
}

void ktimer_run_expired(uint64_t now)
{
	irq_flags_t flags;

	for (;;) {
		struct ktimer *timer;
		ktimer_fn_t function;
		void *arg;

		spin_lock_irqsave(&ktimer_queue.lock, &flags);
		timer = ktimer_detach_first_expired_locked(now);
		if (!timer) {
			spin_unlock_irqrestore(&ktimer_queue.lock, flags);
			break;
		}

		function = timer->function;
		arg = timer->arg;

		if (timer->interval != 0) {
			timer->expires = ktimer_next_interval_deadline(
				timer->expires, timer->interval, now);
			timer->active = true;
			ktimer_insert_locked(timer);
		}

		spin_unlock_irqrestore(&ktimer_queue.lock, flags);
		if (function)
			function(timer, arg);
	}
}

void itimer_state_init(struct itimer_state *state)
{
	state->lock.locked = 0;
	state->value = (struct itimerval){0};
	ktimer_init(&state->timer, itimer_real_fire, NULL);
	state->target = NULL;
}

void itimer_state_destroy(struct itimer_state *state)
{
	bool cancelled = ktimer_cancel(&state->timer);

	(void)cancelled;
	state->value = (struct itimerval){0};
	state->target = NULL;
}

int itimer_get_value(struct itimer_state *state, struct itimerval *value)
{
	irq_flags_t flags;

	spin_lock_irqsave(&state->lock, &flags);
	itimer_snapshot_locked(state, value, arch_timer_now());
	spin_unlock_irqrestore(&state->lock, flags);
	return 0;
}

int itimer_set_real(struct itimer_state *state, struct task_struct *target,
		    const struct itimerval *new_value,
		    struct itimerval *old_value)
{
	uint64_t value_delta;
	uint64_t interval_delta;
	irq_flags_t flags;
	uint64_t now;
	bool cancelled;
	int ret;

	ret = timeval_to_mtime_delta(&new_value->it_value, &value_delta);
	if (ret < 0)
		return ret;
	ret = timeval_to_mtime_delta(&new_value->it_interval, &interval_delta);
	if (ret < 0)
		return ret;

	spin_lock_irqsave(&state->lock, &flags);
	now = arch_timer_now();
	if (old_value)
		itimer_snapshot_locked(state, old_value, now);
	cancelled = ktimer_cancel(&state->timer);

	(void)cancelled;
	if (itimerval_value_is_zero(new_value)) {
		state->value = (struct itimerval){0};
		state->target = NULL;
		spin_unlock_irqrestore(&state->lock, flags);
		return 0;
	}

	state->value = *new_value;
	state->target = target;
	ret = ktimer_arm(&state->timer, mtime_deadline_after(now, value_delta),
			 interval_delta);
	spin_unlock_irqrestore(&state->lock, flags);
	return ret;
}

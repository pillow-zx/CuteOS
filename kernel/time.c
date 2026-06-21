/*
 * kernel/time.c - 时间子系统
 *
 * 功能：
 *   提供时间相关系统调用和内核时间工具函数。管理全局 jiffies 计数器，
 *   实现用户态程序可调用的时间服务。
 *
 * 主要函数：
 *   sys_times(buf)           - times 系统调用。基于 jiffies 返回进程的
 *                              用户态时间和内核态时间（tms_utime /
 * tms_stime）， 以及 cutime / cstime（子进程累计时间）。 sys_gettimeofday(tv,
 * tz) - gettimeofday 系统调用。 读取 CLINT 的 mtime 寄存器， 将其转换为秒 +
 * 微秒精度的时间值。
 */

#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/types.h>
#include <asm/trap.h>

#define CLOCK_REALTIME	0
#define CLOCK_MONOTONIC 1
#define CLOCK_BOOTTIME	7

#define TIMER_ABSTIME 1

struct sys_tms {
	int64_t tms_utime;
	int64_t tms_stime;
	int64_t tms_cutime;
	int64_t tms_cstime;
};

struct sys_timeval {
	int64_t tv_sec;
	int64_t tv_usec;
};

struct sys_timezone {
	int32_t tz_minuteswest;
	int32_t tz_dsttime;
};

struct sys_timespec {
	int64_t tv_sec;
	int64_t tv_nsec;
};

static bool clock_id_supported(int clock_id)
{
	return clock_id == CLOCK_REALTIME || clock_id == CLOCK_MONOTONIC ||
	       clock_id == CLOCK_BOOTTIME;
}

static uint64_t nsec_from_mtime_remainder(uint64_t ticks)
{
	return ticks * 1000000000UL / MTIME_FREQ;
}

static void mtime_to_timespec(uint64_t ticks, struct sys_timespec *ts)
{
	uint64_t sec = ticks / MTIME_FREQ;
	uint64_t rem = ticks % MTIME_FREQ;

	ts->tv_sec = (int64_t)sec;
	ts->tv_nsec = (int64_t)nsec_from_mtime_remainder(rem);
}

static int timespec_to_mtime_delta(const struct sys_timespec *ts,
				   uint64_t *delta)
{
	uint64_t sec_ticks;
	uint64_t nsec_ticks;

	if (!ts || !delta)
		return -EINVAL;
	if (ts->tv_sec < 0 || ts->tv_nsec < 0 ||
	    ts->tv_nsec >= 1000000000LL)
		return -EINVAL;

	if ((uint64_t)ts->tv_sec > UINT64_MAX / MTIME_FREQ)
		sec_ticks = UINT64_MAX;
	else
		sec_ticks = (uint64_t)ts->tv_sec * MTIME_FREQ;

	nsec_ticks =
		((uint64_t)ts->tv_nsec * MTIME_FREQ + 999999999ULL) /
		1000000000ULL;
	if (nsec_ticks > UINT64_MAX - sec_ticks)
		*delta = UINT64_MAX;
	else
		*delta = sec_ticks + nsec_ticks;

	return 0;
}

static uint64_t mtime_deadline_after(uint64_t now, uint64_t delta)
{
	if (delta > UINT64_MAX - now)
		return UINT64_MAX;
	return now + delta;
}

ssize_t sys_times(struct trap_frame *tf)
{
	struct sys_tms *utms = (struct sys_tms *)tf->a0;
	struct sys_tms ktms = {
		.tms_utime = current ? (int64_t)current->utime_ticks : 0,
		.tms_stime = current ? (int64_t)current->stime_ticks : 0,
		.tms_cutime = 0,
		.tms_cstime = 0,
	};

	if (utms && copy_to_user(utms, &ktms, sizeof(ktms)) != 0)
		return -EFAULT;

	return (ssize_t)jiffies;
}

ssize_t sys_gettimeofday(struct trap_frame *tf)
{
	struct sys_timeval *utv = (struct sys_timeval *)tf->a0;
	struct sys_timezone *utz = (struct sys_timezone *)tf->a1;
	uint64_t ticks = get_mtime();
	struct sys_timeval ktv;

	if (utv) {
		ktv.tv_sec = (int64_t)(ticks / MTIME_FREQ);
		ktv.tv_usec = (int64_t)((ticks % MTIME_FREQ) * 1000000UL /
					MTIME_FREQ);
		if (copy_to_user(utv, &ktv, sizeof(ktv)) != 0)
			return -EFAULT;
	}

	if (utz) {
		struct sys_timezone ktz = {0};

		/*
		 * TODO(time): 当前没有 RTC/时区配置，gettimeofday 暂以启动
		 * 时间为 0 点，timezone 始终返回 UTC。
		 */
		if (copy_to_user(utz, &ktz, sizeof(ktz)) != 0)
			return -EFAULT;
	}

	return 0;
}

ssize_t sys_clock_gettime(struct trap_frame *tf)
{
	int clock_id = (int)tf->a0;
	struct sys_timespec *uts = (struct sys_timespec *)tf->a1;
	struct sys_timespec kts;

	if (!clock_id_supported(clock_id))
		return -EINVAL;
	if (!uts)
		return -EFAULT;

	/*
	 * TODO(time): CLOCK_REALTIME 需要 RTC 或启动时 wall-clock offset。
	 * 在当前平台上 REALTIME/MONOTONIC/BOOTTIME 都基于 mtime。
	 */
	mtime_to_timespec(get_mtime(), &kts);
	if (copy_to_user(uts, &kts, sizeof(kts)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_clock_getres(struct trap_frame *tf)
{
	int clock_id = (int)tf->a0;
	struct sys_timespec *uts = (struct sys_timespec *)tf->a1;
	struct sys_timespec kts = {
		.tv_sec = 0,
		.tv_nsec = 1000000000UL / MTIME_FREQ,
	};

	if (!clock_id_supported(clock_id))
		return -EINVAL;
	if (!uts)
		return 0;
	if (copy_to_user(uts, &kts, sizeof(kts)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_nanosleep(struct trap_frame *tf)
{
	const struct sys_timespec *ureq = (const struct sys_timespec *)tf->a0;
	struct sys_timespec *urem = (struct sys_timespec *)tf->a1;
	struct sys_timespec req;
	uint64_t now;
	uint64_t deadline;
	uint64_t delta;
	int ret;

	if (!ureq)
		return -EFAULT;
	if (copy_from_user(&req, ureq, sizeof(req)) != 0)
		return -EFAULT;

	ret = timespec_to_mtime_delta(&req, &delta);
	if (ret < 0)
		return ret;

	now = get_mtime();
	deadline = mtime_deadline_after(now, delta);

	ret = timer_sleep_until(deadline, true);
	if (ret == -EINTR && urem) {
		struct sys_timespec rem = {0};
		uint64_t after = get_mtime();

		if (deadline > after)
			mtime_to_timespec(deadline - after, &rem);
		if (copy_to_user(urem, &rem, sizeof(rem)) != 0)
			return -EFAULT;
	}

	return ret;
}

ssize_t sys_getitimer(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(time): 需要进程级 interval timer 状态。 */
	return -ENOSYS;
}

ssize_t sys_setitimer(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(time): 需要进程级 interval timer 状态和 SIGALRM 投递。 */
	return -ENOSYS;
}

ssize_t sys_timer_create(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(time): 需要 POSIX timer 对象、ID 分配和到期投递机制。 */
	return -ENOSYS;
}

ssize_t sys_timer_gettime(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(time): 需要 POSIX timer 对象状态。 */
	return -ENOSYS;
}

ssize_t sys_timer_getoverrun(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(time): 需要 POSIX timer overrun 计数。 */
	return -ENOSYS;
}

ssize_t sys_timer_settime(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(time): 需要 POSIX timer 对象和基于 timer 的到期唤醒。 */
	return -ENOSYS;
}

ssize_t sys_timer_delete(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(time): 需要 POSIX timer 对象生命周期管理。 */
	return -ENOSYS;
}

ssize_t sys_clock_settime(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(time): 当前没有 RTC 或可写 wall-clock offset。 */
	return -ENOSYS;
}

ssize_t sys_clock_nanosleep(struct trap_frame *tf)
{
	int clock_id = (int)tf->a0;
	int flags = (int)tf->a1;
	const struct sys_timespec *ureq = (const struct sys_timespec *)tf->a2;
	struct sys_timespec *urem = (struct sys_timespec *)tf->a3;
	struct sys_timespec req;
	uint64_t now;
	uint64_t deadline;
	uint64_t delta;
	int ret;

	if (!clock_id_supported(clock_id))
		return -EINVAL;
	if (!ureq)
		return -EFAULT;
	if (copy_from_user(&req, ureq, sizeof(req)) != 0)
		return -EFAULT;

	ret = timespec_to_mtime_delta(&req, &delta);
	if (ret < 0)
		return ret;

	if (flags == TIMER_ABSTIME) {
		deadline = delta;
	} else if (flags == 0) {
		now = get_mtime();
		deadline = mtime_deadline_after(now, delta);
	} else {
		return -EINVAL;
	}

	ret = timer_sleep_until(deadline, true);
	if (ret == -EINTR && flags == 0 && urem) {
		struct sys_timespec rem = {0};
		uint64_t after = get_mtime();

		if (deadline > after)
			mtime_to_timespec(deadline - after, &rem);
		if (copy_to_user(urem, &rem, sizeof(rem)) != 0)
			return -EFAULT;
	}

	return ret;
}

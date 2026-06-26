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
 *   mtime_to_timespec(ticks, ts) - mtime tick → struct sys_timespec
 *   timespec_to_mtime_delta(ts, delta) - struct sys_timespec → tick 增量
 *   mtime_deadline_after(now, delta) - 带饱和的截止时间计算
 */

#include <kernel/errno.h>
#include <kernel/time.h>
#include <kernel/timer.h>
#include <kernel/types.h>

#define CLOCK_REALTIME	0
#define CLOCK_MONOTONIC 1
#define CLOCK_BOOTTIME	7

static uint64_t nsec_from_mtime_remainder(uint64_t ticks)
{
	return ticks * 1000000000UL / MTIME_FREQ;
}

bool clock_id_supported(int clock_id)
{
	return clock_id == CLOCK_REALTIME || clock_id == CLOCK_MONOTONIC ||
	       clock_id == CLOCK_BOOTTIME;
}

void mtime_to_timespec(uint64_t ticks, struct sys_timespec *ts)
{
	uint64_t sec = ticks / MTIME_FREQ;
	uint64_t rem = ticks % MTIME_FREQ;

	ts->tv_sec = (int64_t)sec;
	ts->tv_nsec = (int64_t)nsec_from_mtime_remainder(rem);
}

int timespec_to_mtime_delta(const struct sys_timespec *ts, uint64_t *delta)
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

uint64_t mtime_deadline_after(uint64_t now, uint64_t delta)
{
	if (delta > UINT64_MAX - now)
		return UINT64_MAX;
	return now + delta;
}

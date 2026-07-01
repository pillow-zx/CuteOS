/*
 * syscall/sys_time.c - 时间相关系统调用（ABI 边界层）
 *
 * 功能：
 *   实现时间相关的系统调用。本文件属于 ABI 边界层：负责从 trap_frame
 *   解包参数、验证用户指针，并委托给 kernel/time.c 中的核心逻辑。
 *
 *   所有 stub 实现原地返回 -ENOSYS，并附有待实现的 TODO 注释。
 *
 * 主要函数：
 *   sys_times(tf)              - times 系统调用
 *   sys_gettimeofday(tf, tz)   - gettimeofday 系统调用
 *   sys_clock_gettime(tf)      - 获取指定时钟的时间
 *   sys_clock_getres(tf)       - 获取指定时钟的分辨率
 *   sys_nanosleep(tf)          - 高精度休眠
 *   sys_clock_nanosleep(tf)    - 基于指定时钟的休眠
 *   sys_getitimer/setitimer/sys_timer_* - 均返回 -ENOSYS
 *   sys_clock_settime(tf)      - 返回 Linux 兼容的显式不支持错误
 */

#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/timer.h>
#include <kernel/types.h>
#include <asm/trap.h>

ssize_t sys_times(struct trap_frame *tf)
{
	struct tms *utms = (struct tms *)tf->a0;
	struct tms ktms = {
		.tms_utime = (int64_t)task_user_ticks(current),
		.tms_stime = (int64_t)task_system_ticks(current),
		.tms_cutime = 0,
		.tms_cstime = 0,
	};

	if (utms && copy_to_user(utms, &ktms, sizeof(ktms)) != 0)
		return -EFAULT;

	return (ssize_t)jiffies;
}

ssize_t sys_gettimeofday(struct trap_frame *tf)
{
	struct timeval *utv = (struct timeval *)tf->a0;
	struct timezone *utz = (struct timezone *)tf->a1;
	uint64_t ticks = arch_timer_now();
	struct timeval ktv;

	if (utv) {
		ktv.tv_sec = (int64_t)(ticks / MTIME_FREQ);
		ktv.tv_usec = (int64_t)((ticks % MTIME_FREQ) * 1000000UL /
					MTIME_FREQ);
		if (copy_to_user(utv, &ktv, sizeof(ktv)) != 0)
			return -EFAULT;
	}

	if (utz) {
		struct timezone ktz = {0};

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
	struct timespec *uts = (struct timespec *)tf->a1;
	struct timespec kts;

	if (!clock_id_supported(clock_id))
		return -EINVAL;
	if (!uts)
		return -EFAULT;

	/*
	 * TODO(time): CLOCK_REALTIME 需要 RTC 或启动时 wall-clock offset。
	 * 在当前平台上 REALTIME/MONOTONIC/BOOTTIME 都基于 mtime。
	 */
	mtime_to_timespec(arch_timer_now(), &kts);
	if (copy_to_user(uts, &kts, sizeof(kts)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_clock_getres(struct trap_frame *tf)
{
	int clock_id = (int)tf->a0;
	struct timespec *uts = (struct timespec *)tf->a1;
	struct timespec kts = {
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
	const struct timespec *ureq = (const struct timespec *)tf->a0;
	struct timespec *urem = (struct timespec *)tf->a1;
	struct timespec req;
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

	now = arch_timer_now();
	deadline = mtime_deadline_after(now, delta);

	ret = timer_sleep_until(deadline, true);
	if (ret == -EINTR && urem) {
		struct timespec rem = {0};
		uint64_t after = arch_timer_now();

		if (deadline > after)
			mtime_to_timespec(deadline - after, &rem);
		if (copy_to_user(urem, &rem, sizeof(rem)) != 0)
			return -EFAULT;
	}

	return ret;
}

ssize_t sys_clock_nanosleep(struct trap_frame *tf)
{
	int clock_id = (int)tf->a0;
	int flags = (int)tf->a1;
	const struct timespec *ureq = (const struct timespec *)tf->a2;
	struct timespec *urem = (struct timespec *)tf->a3;
	struct timespec req;
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

	if (flags == TIMER_ABSTIME)
		deadline = delta;
	else if (flags == 0) {
		now = arch_timer_now();
		deadline = mtime_deadline_after(now, delta);
	} else
		return -EINVAL;

	ret = timer_sleep_until(deadline, true);
	if (ret == -EINTR && flags == 0 && urem) {
		struct timespec rem = {0};
		uint64_t after = arch_timer_now();

		if (deadline > after)
			mtime_to_timespec(deadline - after, &rem);
		if (copy_to_user(urem, &rem, sizeof(rem)) != 0)
			return -EFAULT;
	}

	return ret;
}

/* ---- stubs ---- */

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
	int clock_id = (int)tf->a0;
	const struct timespec *uts = (const struct timespec *)tf->a1;
	struct timespec kts;

	if (!clock_id_supported(clock_id))
		return -EINVAL;
	if (!uts)
		return -EFAULT;
	if (copy_from_user(&kts, uts, sizeof(kts)) != 0)
		return -EFAULT;
	if (kts.tv_sec < 0 || kts.tv_nsec < 0 || kts.tv_nsec >= 1000000000LL)
		return -EINVAL;

	switch (clock_id) {
	case CLOCK_REALTIME:
		/*
		 * cuteOS has no CAP_SYS_TIME model and no writable wall-clock
		 * offset.  The syscall exists, but user space cannot mutate it.
		 */
		return -EPERM;
	case CLOCK_MONOTONIC:
	case CLOCK_BOOTTIME:
	default:
		return -EINVAL;
	}
}

/*
 * syscall/sys_time.c - 时间相关系统调用（ABI 边界层）
 */

#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/signal.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/timer.h>
#include <kernel/types.h>
#include <kernel/trap.h>

ssize_t sys_times(struct trap_frame *tf)
{
	struct tms *utms = (struct tms *)syscall_arg(tf, 0);
	struct tms ktms = {
		.tms_utime = (int64_t)task_user_ticks(current_task()),
		.tms_stime = (int64_t)task_system_ticks(current_task()),
		.tms_cutime = 0,
		.tms_cstime = 0,
	};

	if (utms && copy_to_user(utms, &ktms, sizeof(ktms)) != 0)
		return -EFAULT;

	return (ssize_t)jiffies;
}

ssize_t sys_gettimeofday(struct trap_frame *tf)
{
	struct timeval *utv = (struct timeval *)syscall_arg(tf, 0);
	struct timezone *utz = (struct timezone *)syscall_arg(tf, 1);
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

		if (copy_to_user(utz, &ktz, sizeof(ktz)) != 0)
			return -EFAULT;
	}

	return 0;
}

ssize_t sys_clock_gettime(struct trap_frame *tf)
{
	int clock_id = (int)syscall_arg(tf, 0);
	struct timespec *uts = (struct timespec *)syscall_arg(tf, 1);
	struct timespec kts;

	if (!clock_id_supported(clock_id))
		return -EINVAL;
	if (!uts)
		return -EFAULT;


	mtime_to_timespec(arch_timer_now(), &kts);
	if (copy_to_user(uts, &kts, sizeof(kts)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_clock_getres(struct trap_frame *tf)
{
	int clock_id = (int)syscall_arg(tf, 0);
	struct timespec *uts = (struct timespec *)syscall_arg(tf, 1);
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
	const struct timespec *ureq = (const struct timespec *)syscall_arg(tf, 0);
	struct timespec *urem = (struct timespec *)syscall_arg(tf, 1);
	struct timespec req;
	uint64_t deadline;
	bool has_timeout;
	int ret;

	if (!ureq)
		return -EFAULT;
	if (copy_from_user(&req, ureq, sizeof(req)) != 0)
		return -EFAULT;

	ret = mtime_deadline_from_timespec(&req, &has_timeout, &deadline);
	if (ret < 0)
		return ret;

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
	int clock_id = (int)syscall_arg(tf, 0);
	int flags = (int)syscall_arg(tf, 1);
	const struct timespec *ureq = (const struct timespec *)syscall_arg(tf, 2);
	struct timespec *urem = (struct timespec *)syscall_arg(tf, 3);
	struct timespec req;
	uint64_t deadline;
	uint64_t delta;
	bool has_timeout;
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
		ret = mtime_deadline_from_timespec(&req, &has_timeout,
						   &deadline);
		if (ret < 0)
			return ret;
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

ssize_t sys_getitimer(struct trap_frame *tf)
{
	int which = (int)syscall_arg(tf, 0);
	struct itimerval *uvalue = (struct itimerval *)syscall_arg(tf, 1);
	struct signal_struct *signal;
	struct itimerval value;
	int ret;

	if (!itimer_which_valid(which))
		return -EINVAL;
	if (!uvalue)
		return -EFAULT;

	signal = task_signal_state(current_task());
	if (!signal)
		return -EINVAL;

	ret = itimer_get_value(&signal->itimers[itimer_which_index(which)],
			       &value);
	if (ret < 0)
		return ret;

	if (copy_to_user(uvalue, &value, sizeof(value)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_setitimer(struct trap_frame *tf)
{
	int which = (int)syscall_arg(tf, 0);
	const struct itimerval *unew_value = (const struct itimerval *)syscall_arg(tf, 1);
	struct itimerval *uold_value = (struct itimerval *)syscall_arg(tf, 2);
	struct itimerval new_value = {0};
	struct itimerval old_value;
	struct signal_struct *signal;
	struct task_struct *target;
	int ret;

	if (!itimer_which_valid(which))
		return -EINVAL;
	if (which != ITIMER_REAL)
		return -EINVAL;

	if (unew_value &&
	    copy_from_user(&new_value, unew_value, sizeof(new_value)) != 0)
		return -EFAULT;

	signal = task_signal_state(current_task());
	if (!signal)
		return -EINVAL;

	target = task_group_leader_safe(current_task());
	if (!target)
		target = current_task();
	ret = itimer_set_real(&signal->itimers[ITIMER_REAL], target, &new_value,
			      uold_value ? &old_value : NULL);
	if (ret < 0)
		return ret;

	if (uold_value &&
	    copy_to_user(uold_value, &old_value, sizeof(old_value)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_timer_create(struct trap_frame *tf)
{
	clockid_t clock_id = (clockid_t)syscall_arg(tf, 0);
	const sigevent_t *usevp = (const sigevent_t *)syscall_arg(tf, 1);
	timer_t *utimerid = (timer_t *)syscall_arg(tf, 2);
	sigevent_t event;
	const sigevent_t *eventp = NULL;
	struct signal_struct *signal;
	struct task_struct *target;
	timer_t timerid;
	int ret;

	if (!utimerid)
		return -EFAULT;
	if (usevp) {
		if (copy_from_user(&event, usevp, sizeof(event)) != 0)
			return -EFAULT;
		eventp = &event;
	}

	signal = task_signal_state(current_task());
	if (!signal)
		return -EINVAL;

	target = task_group_leader_safe(current_task());
	if (!target)
		target = current_task();

	ret = posix_timer_create(signal, clock_id, &timerid, eventp, target);
	if (ret < 0)
		return ret;

	if (copy_to_user(utimerid, &timerid, sizeof(timerid)) != 0) {
		int delete_ret = posix_timer_delete(signal, timerid);

		(void)delete_ret;
		return -EFAULT;
	}
	return 0;
}

ssize_t sys_timer_gettime(struct trap_frame *tf)
{
	timer_t timerid = (timer_t)syscall_arg(tf, 0);
	struct itimerspec *uvalue = (struct itimerspec *)syscall_arg(tf, 1);
	struct signal_struct *signal;
	struct itimerspec value;
	int ret;

	if (!uvalue)
		return -EFAULT;

	signal = task_signal_state(current_task());
	if (!signal)
		return -EINVAL;

	ret = posix_timer_gettime(signal, timerid, &value);
	if (ret < 0)
		return ret;

	if (copy_to_user(uvalue, &value, sizeof(value)) != 0)
		return -EFAULT;
	return 0;
}

ssize_t sys_timer_getoverrun(struct trap_frame *tf)
{
	timer_t timerid = (timer_t)syscall_arg(tf, 0);
	struct signal_struct *signal;

	signal = task_signal_state(current_task());
	if (!signal)
		return -EINVAL;
	return posix_timer_getoverrun(signal, timerid);
}

ssize_t sys_timer_settime(struct trap_frame *tf)
{
	timer_t timerid = (timer_t)syscall_arg(tf, 0);
	int flags = (int)syscall_arg(tf, 1);
	const struct itimerspec *unew_value = (const struct itimerspec *)syscall_arg(tf, 2);
	struct itimerspec *uold_value = (struct itimerspec *)syscall_arg(tf, 3);
	struct itimerspec new_value;
	struct itimerspec old_value;
	struct signal_struct *signal;
	int ret;

	if (!unew_value)
		return -EFAULT;
	if (copy_from_user(&new_value, unew_value, sizeof(new_value)) != 0)
		return -EFAULT;

	signal = task_signal_state(current_task());
	if (!signal)
		return -EINVAL;

	ret = posix_timer_settime(signal, timerid, flags, &new_value,
				  uold_value ? &old_value : NULL);
	if (ret < 0)
		return ret;

	if (uold_value &&
	    copy_to_user(uold_value, &old_value, sizeof(old_value)) != 0)
		return -EFAULT;
	return 0;
}

ssize_t sys_timer_delete(struct trap_frame *tf)
{
	timer_t timerid = (timer_t)syscall_arg(tf, 0);
	struct signal_struct *signal;

	signal = task_signal_state(current_task());
	if (!signal)
		return -EINVAL;
	return posix_timer_delete(signal, timerid);
}

ssize_t sys_clock_settime(struct trap_frame *tf)
{
	int clock_id = (int)syscall_arg(tf, 0);
	const struct timespec *uts = (const struct timespec *)syscall_arg(tf, 1);
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

		return -EPERM;
	case CLOCK_MONOTONIC:
	case CLOCK_BOOTTIME:
	default:
		return -EINVAL;
	}
}

#ifndef _CUTEOS_KERNEL_RESOURCE_H
#define _CUTEOS_KERNEL_RESOURCE_H

#include <kernel/compiler.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <uapi/resource.h>

#define CPUTIME_USEC_PER_SEC 1000000UL

static_assert(sizeof(struct rusage) == 144,
	      "struct rusage must match the riscv64 ABI (144 bytes)");
static_assert(offsetof(struct rusage, ru_stime) == 16,
	      "ru_stime offset drifted from riscv64 rusage ABI");
static_assert(offsetof(struct rusage, ru_nivcsw) == 136,
	      "ru_nivcsw offset drifted from riscv64 rusage ABI");

void rlimits_init(struct rlimit64 rlimits[RLIM_NLIMITS]);

static __always_inline __nonnull(1, 2)
	__access_no_size(read_write, 1) __access_no_size(read_only, 2)
void cputime_add(struct task_cputime *dst, const struct task_cputime *src)
{
	dst->utime_ticks += src->utime_ticks;
	dst->stime_ticks += src->stime_ticks;
}

static __always_inline __nonnull(2) __access_no_size(write_only, 2)
void cputime_timeval(uint64_t ticks, struct timeval *tv)
{
	uint64_t sec = ticks / HZ;
	uint64_t rem = ticks % HZ;

	tv->tv_sec = (long)sec;
	tv->tv_usec = (long)(rem * CPUTIME_USEC_PER_SEC / HZ);
}

static __always_inline __nonnull(1, 2)
	__access_no_size(read_only, 1) __access_no_size(write_only, 2)
void cputime_rusage(const struct task_cputime *time, struct rusage *ru)
{
	struct timeval utime;
	struct timeval stime;

	cputime_timeval(time->utime_ticks, &utime);
	cputime_timeval(time->stime_ticks, &stime);

	*ru = (struct rusage){
		.ru_utime = utime,
		.ru_stime = stime,
	};
}

static __always_inline __nonnull(1, 2)
	__access_no_size(read_only, 1) __access_no_size(write_only, 2)
void task_cputime_total(const struct task_struct *task,
			struct task_cputime *time)
{
	*time = (struct task_cputime){
		.utime_ticks = task->cputime.utime_ticks,
		.stime_ticks = task->cputime.stime_ticks,
	};
	cputime_add(time, &task->child_cputime);
}

static __always_inline __nonnull(1, 2)
	__access_no_size(read_write, 1) __access_no_size(read_only, 2)
void task_add_child_time(struct task_struct *task,
			 const struct task_cputime *time)
{
	cputime_add(&task_group_leader(task)->child_cputime, time);
}

static __always_inline __nonnull(1, 2)
	__access_no_size(read_only, 1) __access_no_size(write_only, 2)
void task_rusage_self(const struct task_struct *task, struct rusage *ru)
{
	struct task_cputime time = {
		.utime_ticks = task_user_ticks(task),
		.stime_ticks = task_system_ticks(task),
	};

	cputime_rusage(&time, ru);
}

static __always_inline __nonnull(1, 2)
	__access_no_size(read_only, 1) __access_no_size(write_only, 2)
void task_rusage_children(const struct task_struct *task, struct rusage *ru)
{
	cputime_rusage(&task->ids.group_leader->child_cputime, ru);
}

#endif

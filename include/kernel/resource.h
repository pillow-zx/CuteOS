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

static __always_inline __nonnull(2) __access_no_size(write_only, 2)
void cputime_timeval(uint64_t ticks, struct timeval *tv)
{
	uint64_t sec = ticks / HZ;
	uint64_t rem = ticks % HZ;

	tv->tv_sec = (long)sec;
	tv->tv_usec = (long)(rem * CPUTIME_USEC_PER_SEC / HZ);
}

static __always_inline __nonnull(1, 2) __access_no_size(write_only, 2)
void task_rusage_self(const struct task_struct *task, struct rusage *ru)
{
	struct timeval utime;
	struct timeval stime;

	cputime_timeval(task_user_ticks(task), &utime);
	cputime_timeval(task_system_ticks(task), &stime);

	*ru = (struct rusage){
		.ru_utime = utime,
		.ru_stime = stime,
	};
}

#endif

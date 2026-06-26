#ifndef _CUTEOS_KERNEL_TIME_H
#define _CUTEOS_KERNEL_TIME_H

/*
 * include/kernel/time.h - 时间子系统共享类型与内部 API
 *
 * 本头文件声明 ABI 相关的用户空间时间结构体布局（与 UAPI 协定一致）
 * 以及时间子系统的内部 API，供 kernel/time.c（核心实现）和
 * syscall/sys_time.c（ABI 边界）共享。
 */

#include <kernel/types.h>

/* ---- ABI 结构体布局（与用户空间共享） ---- */

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

/* ---- 内部 API ---- */

bool clock_id_supported(int clock_id);
void mtime_to_timespec(uint64_t ticks, struct sys_timespec *ts);
int timespec_to_mtime_delta(const struct sys_timespec *ts, uint64_t *delta);
uint64_t mtime_deadline_after(uint64_t now, uint64_t delta);

#endif /* _CUTEOS_KERNEL_TIME_H */

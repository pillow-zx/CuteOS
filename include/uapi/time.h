#ifndef _CUTEOS_UAPI_TIME_H
#define _CUTEOS_UAPI_TIME_H

#define CLOCK_REALTIME	0
#define CLOCK_MONOTONIC 1
#define CLOCK_BOOTTIME	7

#define TIMER_ABSTIME 0x01

#define UTIME_NOW  0x3fffffff
#define UTIME_OMIT 0x3ffffffe

struct tms {
	long tms_utime;
	long tms_stime;
	long tms_cutime;
	long tms_cstime;
};

struct timeval {
	long tv_sec;
	long tv_usec;
};

struct timezone {
	int tz_minuteswest;
	int tz_dsttime;
};

struct timespec {
	long tv_sec;
	long tv_nsec;
};

#endif

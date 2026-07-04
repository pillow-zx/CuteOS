#ifndef _CUTEOS_UAPI_TIME_H
#define _CUTEOS_UAPI_TIME_H

typedef int clockid_t;
typedef int timer_t;

#define CLOCK_REALTIME		  0
#define CLOCK_MONOTONIC	  1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3
#define CLOCK_MONOTONIC_RAW	  4
#define CLOCK_REALTIME_COARSE	  5
#define CLOCK_MONOTONIC_COARSE	  6
#define CLOCK_BOOTTIME		  7
#define CLOCK_REALTIME_ALARM	  8
#define CLOCK_BOOTTIME_ALARM	  9
#define CLOCK_SGI_CYCLE	  10
#define CLOCK_TAI		  11
#define MAX_CLOCKS		  16

#define TIMER_ABSTIME 0x01

#define UTIME_NOW  0x3fffffff
#define UTIME_OMIT 0x3ffffffe

#define ITIMER_REAL	0
#define ITIMER_VIRTUAL	1
#define ITIMER_PROF	2

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

struct itimerspec {
	struct timespec it_interval;
	struct timespec it_value;
};

struct itimerval {
	struct timeval it_interval;
	struct timeval it_value;
};

#undef offsetof
#define offsetof(t, d) __builtin_offsetof(t, d)

_Static_assert(sizeof(clockid_t) == 4, "clockid_t ABI size mismatch");
_Static_assert(sizeof(timer_t) == 4, "timer_t ABI size mismatch");
_Static_assert(sizeof(struct timespec) == 16, "timespec ABI size mismatch");
_Static_assert(offsetof(struct timespec, tv_nsec) == 8,
	       "timespec tv_nsec ABI offset mismatch");
_Static_assert(sizeof(struct timeval) == 16, "timeval ABI size mismatch");
_Static_assert(offsetof(struct timeval, tv_usec) == 8,
	       "timeval tv_usec ABI offset mismatch");
_Static_assert(sizeof(struct itimerspec) == 32,
	       "itimerspec ABI size mismatch");
_Static_assert(offsetof(struct itimerspec, it_value) == 16,
	       "itimerspec it_value ABI offset mismatch");
_Static_assert(sizeof(struct itimerval) == 32,
	       "itimerval ABI size mismatch");
_Static_assert(offsetof(struct itimerval, it_value) == 16,
	       "itimerval it_value ABI offset mismatch");

#endif

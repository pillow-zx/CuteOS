/*
 * user/bin/time_test.c - time user ABI tests
 */

#include <ulib.h>

static volatile int time_sigalrm_count;

static void time_sigalrm_handler(int sig)
{
	if (sig == SIGALRM)
		time_sigalrm_count++;
}

static int time_expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int time_expect_itimerval_zero(const char *name,
				      const struct itimerval *value)
{
	if (value->it_interval.tv_sec != 0 || value->it_interval.tv_usec != 0 ||
	    value->it_value.tv_sec != 0 || value->it_value.tv_usec != 0) {
		printf("FAIL: %s expected zero itimerval\n", name);
		return 1;
	}

	return 0;
}

static long time_timeval_usec(const struct timeval *value)
{
	return value->tv_sec * 1000000L + value->tv_usec;
}

static int time_check_getitimer_zero(int which, const char *name)
{
	struct itimerval value = {
		.it_interval = {
			.tv_sec = -1,
			.tv_usec = -1,
		},
		.it_value = {
			.tv_sec = -1,
			.tv_usec = -1,
		},
	};
	int failed = 0;
	long ret;

	ret = syscall(SYS_getitimer, which, (long)&value);
	failed += time_expect_ret(name, ret, 0);
	if (ret == 0)
		failed += time_expect_itimerval_zero(name, &value);

	return failed;
}

static int test_getitimer_zero_state(void)
{
	int failed = 0;

	failed += time_check_getitimer_zero(ITIMER_REAL, "getitimer real");
	failed +=
		time_check_getitimer_zero(ITIMER_VIRTUAL, "getitimer virtual");
	failed += time_check_getitimer_zero(ITIMER_PROF, "getitimer prof");

	return failed;
}

static int test_getitimer_errors(void)
{
	struct itimerval value;
	int failed = 0;

	failed += time_expect_ret("getitimer invalid which",
				  syscall(SYS_getitimer, 99, (long)&value),
				  -EINVAL);
	failed += time_expect_ret("getitimer null value",
				  syscall(SYS_getitimer, ITIMER_REAL,
					  (long)NULL),
				  -EFAULT);

	return failed;
}

static int test_setitimer_real_old_value_disarm(void)
{
	struct itimerval timer = {
		.it_interval = {
			.tv_sec = 0,
			.tv_usec = 0,
		},
		.it_value = {
			.tv_sec = 0,
			.tv_usec = 300000,
		},
	};
	struct itimerval zero = {0};
	struct itimerval old = {
		.it_interval = {
			.tv_sec = -1,
			.tv_usec = -1,
		},
		.it_value = {
			.tv_sec = -1,
			.tv_usec = -1,
		},
	};
	struct itimerval current;
	struct timespec wait = {
		.tv_sec = 0,
		.tv_nsec = 50000000,
	};
	long old_usec;
	int failed = 0;

	failed += time_expect_ret("setitimer real old arm",
				  syscall(SYS_setitimer, ITIMER_REAL,
					  (long)&timer, (long)NULL),
				  0);
	failed += time_expect_ret("short nanosleep before disarm",
				  nanosleep(&wait, NULL), -ETIMEDOUT);
	failed += time_expect_ret("setitimer real disarm old",
				  syscall(SYS_setitimer, ITIMER_REAL,
					  (long)&zero, (long)&old),
				  0);

	old_usec = time_timeval_usec(&old.it_value);
	if (old_usec <= 0 || old_usec >= 300000) {
		printf("FAIL: old it_value expected remaining < 300000 got %ld\n",
		       old_usec);
		failed++;
	}

	failed += time_expect_ret("getitimer real after disarm",
				  syscall(SYS_getitimer, ITIMER_REAL,
					  (long)&current),
				  0);
	failed += time_expect_itimerval_zero("getitimer real after disarm",
					     &current);

	return failed;
}

static int test_setitimer_real_repeating_signal(void)
{
	struct sigaction sa = {
		.sa_handler = time_sigalrm_handler,
		.sa_flags = 0,
		.sa_restorer = NULL,
		.sa_mask = 0,
	};
	struct itimerval timer = {
		.it_interval = {
			.tv_sec = 0,
			.tv_usec = 40000,
		},
		.it_value = {
			.tv_sec = 0,
			.tv_usec = 40000,
		},
	};
	struct itimerval zero = {0};
	struct timespec wait = {
		.tv_sec = 0,
		.tv_nsec = 200000000,
	};
	int failed = 0;

	time_sigalrm_count = 0;
	failed += time_expect_ret("sigaction repeat sigalrm",
				  sigaction(SIGALRM, &sa, NULL), 0);
	failed += time_expect_ret("setitimer real repeat",
				  syscall(SYS_setitimer, ITIMER_REAL,
					  (long)&timer, (long)NULL),
				  0);
	failed += time_expect_ret("first repeat sigalrm",
				  nanosleep(&wait, NULL), -EINTR);
	failed += time_expect_ret("second repeat sigalrm",
				  nanosleep(&wait, NULL), -EINTR);
	failed += time_expect_ret("disarm repeating timer",
				  syscall(SYS_setitimer, ITIMER_REAL,
					  (long)&zero, (long)NULL),
				  0);
	if (time_sigalrm_count < 2) {
		printf("FAIL: repeating sigalrm count expected >=2 got %d\n",
		       time_sigalrm_count);
		failed++;
	}

	return failed;
}

static int test_setitimer_errors(void)
{
	struct itimerval valid = {
		.it_interval = {
			.tv_sec = 0,
			.tv_usec = 0,
		},
		.it_value = {
			.tv_sec = 0,
			.tv_usec = 200000,
		},
	};
	struct itimerval bad_usec = {
		.it_interval = {
			.tv_sec = 0,
			.tv_usec = 0,
		},
		.it_value = {
			.tv_sec = 0,
			.tv_usec = 1000000,
		},
	};
	struct itimerval bad_negative = {
		.it_interval = {
			.tv_sec = 0,
			.tv_usec = 0,
		},
		.it_value = {
			.tv_sec = -1,
			.tv_usec = 0,
		},
	};
	struct itimerval value;
	int failed = 0;

	failed += time_expect_ret("setitimer invalid which",
				  syscall(SYS_setitimer, 99, (long)&valid,
					  (long)NULL),
				  -EINVAL);
	failed += time_expect_ret("setitimer virtual unsupported",
				  syscall(SYS_setitimer, ITIMER_VIRTUAL,
					  (long)&valid, (long)NULL),
				  -EINVAL);
	failed += time_expect_ret("setitimer bad usec",
				  syscall(SYS_setitimer, ITIMER_REAL,
					  (long)&bad_usec, (long)NULL),
				  -EINVAL);
	failed += time_expect_ret("setitimer negative time",
				  syscall(SYS_setitimer, ITIMER_REAL,
					  (long)&bad_negative, (long)NULL),
				  -EINVAL);
	failed += time_expect_ret("setitimer bad new pointer",
				  syscall(SYS_setitimer, ITIMER_REAL,
					  (long)~0UL, (long)NULL),
				  -EFAULT);

	failed += time_expect_ret("setitimer null new disarm arm",
				  syscall(SYS_setitimer, ITIMER_REAL,
					  (long)&valid, (long)NULL),
				  0);
	failed += time_expect_ret("setitimer null new disarm",
				  syscall(SYS_setitimer, ITIMER_REAL,
					  (long)NULL, (long)NULL),
				  0);
	failed += time_expect_ret("getitimer after null new disarm",
				  syscall(SYS_getitimer, ITIMER_REAL,
					  (long)&value),
				  0);
	failed += time_expect_itimerval_zero("getitimer after null new disarm",
					     &value);

	failed += time_expect_ret("setitimer bad old pointer applies",
				  syscall(SYS_setitimer, ITIMER_REAL,
					  (long)&valid, (long)~0UL),
				  -EFAULT);
	failed += time_expect_ret("setitimer cleanup after bad old",
				  syscall(SYS_setitimer, ITIMER_REAL,
					  (long)NULL, (long)NULL),
				  0);

	return failed;
}

static int test_setitimer_real_oneshot_signal(void)
{
	struct sigaction sa = {
		.sa_handler = time_sigalrm_handler,
		.sa_flags = 0,
		.sa_restorer = NULL,
		.sa_mask = 0,
	};
	struct itimerval timer = {
		.it_interval = {
			.tv_sec = 0,
			.tv_usec = 0,
		},
		.it_value = {
			.tv_sec = 0,
			.tv_usec = 20000,
		},
	};
	struct timespec wait = {
		.tv_sec = 0,
		.tv_nsec = 200000000,
	};
	int failed = 0;
	long ret;

	time_sigalrm_count = 0;
	failed += time_expect_ret("sigaction sigalrm",
				  sigaction(SIGALRM, &sa, NULL), 0);
	ret = syscall(SYS_setitimer, ITIMER_REAL, (long)&timer, (long)NULL);
	failed += time_expect_ret("setitimer real oneshot", ret, 0);
	if (ret != 0)
		return failed;

	failed += time_expect_ret("nanosleep interrupted by sigalrm",
				  nanosleep(&wait, NULL), -EINTR);
	if (time_sigalrm_count != 1) {
		printf("FAIL: sigalrm count expected 1 got %d\n",
		       time_sigalrm_count);
		failed++;
	}

	return failed;
}

static int test_clock_settime_errors(void)
{
	struct timespec valid = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};
	struct timespec invalid_nsec = {
		.tv_sec = 1,
		.tv_nsec = 1000000000L,
	};
	int failed = 0;

	failed +=
		time_expect_ret("realtime unsupported",
				clock_settime(CLOCK_REALTIME, &valid), -EPERM);
	failed += time_expect_ret("monotonic unsettable",
				  clock_settime(CLOCK_MONOTONIC, &valid),
				  -EINVAL);
	failed +=
		time_expect_ret("boottime unsettable",
				clock_settime(CLOCK_BOOTTIME, &valid), -EINVAL);
	failed += time_expect_ret("unknown clock", clock_settime(99, &valid),
				  -EINVAL);
	failed += time_expect_ret("invalid nsec",
				  clock_settime(CLOCK_REALTIME, &invalid_nsec),
				  -EINVAL);
	failed += time_expect_ret("null timespec",
				  clock_settime(CLOCK_REALTIME, NULL), -EFAULT);

	return failed;
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("time_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(void)
{
	int failed = 0;

	report_group("getitimer zero state", test_getitimer_zero_state(),
		     &failed);
	report_group("getitimer error paths", test_getitimer_errors(),
		     &failed);
	report_group("setitimer real oneshot signal",
		     test_setitimer_real_oneshot_signal(), &failed);
	report_group("setitimer real old value disarm",
		     test_setitimer_real_old_value_disarm(), &failed);
	report_group("setitimer real repeating signal",
		     test_setitimer_real_repeating_signal(), &failed);
	report_group("setitimer error paths", test_setitimer_errors(),
		     &failed);
	report_group("clock_settime error paths", test_clock_settime_errors(),
		     &failed);

	if (failed)
		printf("time_test: %d test group(s) FAILED\n", failed);
	else
		printf("time_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

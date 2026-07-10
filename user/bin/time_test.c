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

static long time_timespec_nsec(const struct timespec *value)
{
	return value->tv_sec * 1000000000L + value->tv_nsec;
}

static void time_timespec_add_nsec(struct timespec *value, long nsec)
{
	value->tv_nsec += nsec;
	while (value->tv_nsec >= 1000000000L) {
		value->tv_sec++;
		value->tv_nsec -= 1000000000L;
	}
}

static int time_expect_relative_remainder(const char *name,
					  const struct timespec *request,
					  const struct timespec *remaining)
{
	long request_nsec = time_timespec_nsec(request);
	long remaining_nsec = time_timespec_nsec(remaining);

	if (remaining_nsec <= 0 || remaining_nsec > request_nsec) {
		printf("FAIL: %s remainder=%ld request=%ld\n", name,
		       remaining_nsec, request_nsec);
		return 1;
	}

	return 0;
}

static int time_arm_sigalrm(long usec)
{
	struct itimerval timer = {
		.it_value = {
			.tv_sec = usec / 1000000L,
			.tv_usec = usec % 1000000L,
		},
	};

	return syscall(SYS_setitimer, ITIMER_REAL, (long)&timer, (long)NULL);
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
				  nanosleep(&wait, NULL), 0);
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

static int test_sleep_signal_interruption(void)
{
	struct sigaction sa = {
		.sa_handler = time_sigalrm_handler,
		.sa_flags = SA_RESTART,
	};
	struct timespec request = {
		.tv_sec = 0,
		.tv_nsec = 200000000,
	};
	struct timespec remaining;
	struct timespec absolute;
	struct timespec sentinel = {
		.tv_sec = 123,
		.tv_nsec = 456,
	};
	int failed = 0;

	time_sigalrm_count = 0;
	failed += time_expect_ret("sigaction SA_RESTART sleep",
				  sigaction(SIGALRM, &sa, NULL), 0);

	remaining.tv_sec = -1;
	remaining.tv_nsec = -1;
	failed += time_expect_ret("arm nanosleep signal",
				  time_arm_sigalrm(20000), 0);
	failed += time_expect_ret("nanosleep SA_RESTART interruption",
				  nanosleep(&request, &remaining), -EINTR);
	failed += time_expect_relative_remainder("nanosleep", &request,
						 &remaining);

	remaining.tv_sec = -1;
	remaining.tv_nsec = -1;
	failed += time_expect_ret("arm relative clock sleep signal",
				  time_arm_sigalrm(20000), 0);
	failed += time_expect_ret("relative clock_nanosleep interruption",
				  clock_nanosleep(CLOCK_MONOTONIC, 0,
						  &request, &remaining),
				  -EINTR);
	failed += time_expect_relative_remainder("relative clock_nanosleep",
						 &request, &remaining);

	failed += time_expect_ret("clock_gettime absolute sleep",
				  clock_gettime(CLOCK_MONOTONIC, &absolute), 0);
	time_timespec_add_nsec(&absolute, time_timespec_nsec(&request));
	remaining = sentinel;
	failed += time_expect_ret("arm absolute clock sleep signal",
				  time_arm_sigalrm(20000), 0);
	failed += time_expect_ret("absolute clock_nanosleep interruption",
				  clock_nanosleep(CLOCK_MONOTONIC,
						  TIMER_ABSTIME, &absolute,
						  &remaining),
				  -EINTR);
	if (remaining.tv_sec != sentinel.tv_sec ||
	    remaining.tv_nsec != sentinel.tv_nsec) {
		printf("FAIL: absolute clock_nanosleep modified remainder\n");
		failed++;
	}

	if (time_sigalrm_count != 3) {
		printf("FAIL: sleep interruption signal count expected 3 got %d\n",
		       time_sigalrm_count);
		failed++;
	}

	return failed;
}

static int test_timer_create_sigev_none(void)
{
	struct sigevent sev = {
		.sigev_notify = SIGEV_NONE,
	};
	struct itimerspec current = {
		.it_interval = {
			.tv_sec = -1,
			.tv_nsec = -1,
		},
		.it_value = {
			.tv_sec = -1,
			.tv_nsec = -1,
		},
	};
	timer_t timerid = -1;
	int failed = 0;
	long ret;

	ret = syscall(SYS_timer_create, CLOCK_MONOTONIC, (long)&sev,
		      (long)&timerid);
	failed += time_expect_ret("timer_create sigev none", ret, 0);
	if (ret == 0 && timerid < 0) {
		printf("FAIL: timer_create returned invalid timer id %d\n",
		       timerid);
		failed++;
	}
	if (ret == 0) {
		failed += time_expect_ret("timer_gettime inactive",
					  syscall(SYS_timer_gettime, timerid,
						  (long)&current),
					  0);
		if (current.it_interval.tv_sec != 0 ||
		    current.it_interval.tv_nsec != 0 ||
		    current.it_value.tv_sec != 0 ||
		    current.it_value.tv_nsec != 0) {
			printf("FAIL: inactive timer expected zero itimerspec\n");
			failed++;
		}
	}
	if (timerid >= 0)
		failed += time_expect_ret("timer_delete inactive",
					  syscall(SYS_timer_delete, timerid),
					  0);

	return failed;
}

static int test_timer_settime_relative_gettime(void)
{
	struct sigevent sev = {
		.sigev_notify = SIGEV_NONE,
	};
	struct itimerspec armed = {
		.it_interval = {
			.tv_sec = 0,
			.tv_nsec = 0,
		},
		.it_value = {
			.tv_sec = 0,
			.tv_nsec = 300000000,
		},
	};
	struct itimerspec old = {
		.it_interval = {
			.tv_sec = -1,
			.tv_nsec = -1,
		},
		.it_value = {
			.tv_sec = -1,
			.tv_nsec = -1,
		},
	};
	struct itimerspec current;
	timer_t timerid = -1;
	long remaining;
	int failed = 0;

	failed += time_expect_ret("timer_create for settime",
				  syscall(SYS_timer_create, CLOCK_MONOTONIC,
					  (long)&sev, (long)&timerid),
				  0);
	if (failed)
		goto out;

	failed += time_expect_ret("timer_settime relative",
				  syscall(SYS_timer_settime, timerid, 0,
					  (long)&armed, (long)&old),
				  0);
	failed += time_expect_ret("timer_gettime armed",
				  syscall(SYS_timer_gettime, timerid,
					  (long)&current),
				  0);
	if (failed)
		goto out;

	if (old.it_interval.tv_sec != 0 || old.it_interval.tv_nsec != 0 ||
	    old.it_value.tv_sec != 0 || old.it_value.tv_nsec != 0) {
		printf("FAIL: timer_settime old value expected inactive zero\n");
		failed++;
	}

	remaining = time_timespec_nsec(&current.it_value);
	if (remaining <= 0 || remaining > 300000000L) {
		printf("FAIL: timer_gettime remaining out of range: %ld\n",
		       remaining);
		failed++;
	}
	if (current.it_interval.tv_sec != 0 ||
	    current.it_interval.tv_nsec != 0) {
		printf("FAIL: one-shot timer interval expected zero\n");
		failed++;
	}

out:
	if (timerid >= 0)
		failed += time_expect_ret("timer_delete relative",
					  syscall(SYS_timer_delete, timerid),
					  0);
	return failed;
}

static int test_timer_settime_absolute_gettime(void)
{
	struct sigevent sev = {
		.sigev_notify = SIGEV_NONE,
	};
	struct itimerspec armed = {
		.it_interval = {
			.tv_sec = 0,
			.tv_nsec = 0,
		},
	};
	struct itimerspec current;
	timer_t timerid = -1;
	long remaining;
	int failed = 0;

	failed += time_expect_ret("clock_gettime monotonic for abstime",
				  clock_gettime(CLOCK_MONOTONIC,
						&armed.it_value),
				  0);
	time_timespec_add_nsec(&armed.it_value, 300000000L);
	failed += time_expect_ret("timer_create absolute",
				  syscall(SYS_timer_create, CLOCK_MONOTONIC,
					  (long)&sev, (long)&timerid),
				  0);
	if (failed)
		goto out;

	failed += time_expect_ret("timer_settime absolute",
				  syscall(SYS_timer_settime, timerid,
					  TIMER_ABSTIME, (long)&armed,
					  (long)NULL),
				  0);
	failed += time_expect_ret("timer_gettime absolute",
				  syscall(SYS_timer_gettime, timerid,
					  (long)&current),
				  0);
	if (failed)
		goto out;

	remaining = time_timespec_nsec(&current.it_value);
	if (remaining <= 0 || remaining > 300000000L) {
		printf("FAIL: absolute timer remaining out of range: %ld\n",
		       remaining);
		failed++;
	}

out:
	if (timerid >= 0)
		failed += time_expect_ret("timer_delete absolute",
					  syscall(SYS_timer_delete, timerid),
					  0);
	return failed;
}

static int test_timer_default_sigev_signal(void)
{
	struct sigaction sa = {
		.sa_handler = time_sigalrm_handler,
		.sa_flags = 0,
		.sa_restorer = NULL,
		.sa_mask = 0,
	};
	struct itimerspec armed = {
		.it_interval = {
			.tv_sec = 0,
			.tv_nsec = 0,
		},
		.it_value = {
			.tv_sec = 0,
			.tv_nsec = 20000000,
		},
	};
	struct timespec wait = {
		.tv_sec = 0,
		.tv_nsec = 200000000,
	};
	timer_t timerid = -1;
	int failed = 0;

	time_sigalrm_count = 0;
	failed += time_expect_ret("timer sigaction sigalrm",
				  sigaction(SIGALRM, &sa, NULL), 0);
	failed += time_expect_ret("timer_create default sigev",
				  syscall(SYS_timer_create, CLOCK_MONOTONIC,
					  (long)NULL, (long)&timerid),
				  0);
	if (failed)
		goto out;

	failed += time_expect_ret("timer_settime signal",
				  syscall(SYS_timer_settime, timerid, 0,
					  (long)&armed, (long)NULL),
				  0);
	if (failed)
		goto out;

	failed += time_expect_ret("timer signal interrupts nanosleep",
				  nanosleep(&wait, NULL), -EINTR);
	if (time_sigalrm_count != 1) {
		printf("FAIL: POSIX timer SIGALRM count expected 1 got %d\n",
		       time_sigalrm_count);
		failed++;
	}

out:
	if (timerid >= 0)
		failed += time_expect_ret("timer_delete signal",
					  syscall(SYS_timer_delete, timerid),
					  0);
	return failed;
}

static int test_timer_delete_and_getoverrun(void)
{
	struct sigevent sev = {
		.sigev_notify = SIGEV_NONE,
	};
	struct itimerspec current;
	timer_t timerid = -1;
	int failed = 0;

	failed += time_expect_ret("timer_create for delete",
				  syscall(SYS_timer_create, CLOCK_MONOTONIC,
					  (long)&sev, (long)&timerid),
				  0);
	if (failed)
		return failed;

	failed += time_expect_ret("timer_getoverrun initial",
				  syscall(SYS_timer_getoverrun, timerid), 0);
	failed += time_expect_ret("timer_delete",
				  syscall(SYS_timer_delete, timerid), 0);
	failed += time_expect_ret("timer_gettime after delete",
				  syscall(SYS_timer_gettime, timerid,
					  (long)&current),
				  -EINVAL);
	failed += time_expect_ret("timer_getoverrun after delete",
				  syscall(SYS_timer_getoverrun, timerid),
				  -EINVAL);
	failed += time_expect_ret("timer_delete after delete",
				  syscall(SYS_timer_delete, timerid), -EINVAL);

	return failed;
}

static int test_timer_interval_overrun(void)
{
	struct itimerspec armed = {
		.it_interval = {
			.tv_sec = 0,
			.tv_nsec = 20000000,
		},
		.it_value = {
			.tv_sec = 0,
			.tv_nsec = 20000000,
		},
	};
	struct timespec wait = {
		.tv_sec = 0,
		.tv_nsec = 160000000,
	};
	unsigned long block = 1UL << (SIGALRM - 1);
	unsigned long old_mask = 0;
	timer_t timerid = -1;
	long overrun;
	int failed = 0;
	int blocked = 0;

	failed += time_expect_ret("block SIGALRM for overrun",
				  sigprocmask(SIG_BLOCK, &block, &old_mask),
				  0);
	blocked = failed == 0;
	failed += time_expect_ret("timer_create overrun",
				  syscall(SYS_timer_create, CLOCK_MONOTONIC,
					  (long)NULL, (long)&timerid),
				  0);
	if (failed)
		goto out;

	failed += time_expect_ret("timer_settime interval",
				  syscall(SYS_timer_settime, timerid, 0,
					  (long)&armed, (long)NULL),
				  0);
	failed += time_expect_ret("blocked timer wait",
				  nanosleep(&wait, NULL), 0);
	overrun = syscall(SYS_timer_getoverrun, timerid);

	if (!failed && overrun <= 0) {
		printf("FAIL: timer_getoverrun expected >0 got %ld\n",
		       overrun);
		failed++;
	}

out:
	if (timerid >= 0)
		failed += time_expect_ret("timer_delete overrun",
					  syscall(SYS_timer_delete, timerid),
					  0);
	if (blocked)
		failed += time_expect_ret("restore SIGALRM after overrun",
					  sigprocmask(SIG_SETMASK, &old_mask,
						      NULL),
					  0);
	return failed;
}

static int check_exec_timer_gone(int argc, char **argv)
{
	struct itimerspec current;
	timer_t timerid;
	long ret;

	if (argc != 3) {
		printf("time_test: expected timer id argument\n");
		return 2;
	}

	timerid = (timer_t)atoi(argv[2]);
	ret = syscall(SYS_timer_gettime, timerid, (long)&current);
	if (ret != -EINVAL) {
		printf("time_test: exec timer id %d expected -EINVAL got %ld\n",
		       timerid, ret);
		return 1;
	}

	return 0;
}

static int test_timer_exec_deletes_timer(void)
{
	struct sigevent sev = {
		.sigev_notify = SIGEV_NONE,
	};
	struct itimerspec armed = {
		.it_interval = {
			.tv_sec = 0,
			.tv_nsec = 0,
		},
		.it_value = {
			.tv_sec = 1,
			.tv_nsec = 0,
		},
	};
	char timer_buf[16];
	char *argv[] = {"time_test", "--check-exec-timer", timer_buf, 0};
	char *envp[] = {"PATH=/bin", 0};
	timer_t timerid = -1;
	int status = 0;
	long waited;
	long pid;

	pid = fork();
	if (pid < 0) {
		printf("FAIL: timer exec fork: %ld\n", pid);
		return 1;
	}
	if (pid == 0) {
		if (syscall(SYS_timer_create, CLOCK_MONOTONIC, (long)&sev,
			    (long)&timerid) != 0)
			exit(4);
		if (syscall(SYS_timer_settime, timerid, 0, (long)&armed,
			    (long)NULL) != 0)
			exit(5);

		snprintf(timer_buf, sizeof(timer_buf), "%d", timerid);
		execve("/bin/time_test", argv, envp);
		exit(6);
	}

	waited = wait4(pid, &status, 0, NULL);
	if (waited != pid) {
		printf("FAIL: wait timer exec expected %ld got %ld\n", pid,
		       waited);
		return 1;
	}
	if (status != 0) {
		printf("FAIL: timer exec checker status expected 0 got %d\n",
		       status);
		return 1;
	}

	return 0;
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

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc > 1 && streq(argv[1], "--check-exec-timer"))
		return check_exec_timer_gone(argc, argv);

	report_group("getitimer zero state", test_getitimer_zero_state(),
		     &failed);
	report_group("getitimer error paths", test_getitimer_errors(),
		     &failed);
	report_group("setitimer real oneshot signal",
		     test_setitimer_real_oneshot_signal(), &failed);
	report_group("sleep signal interruption",
		     test_sleep_signal_interruption(), &failed);
	report_group("setitimer real old value disarm",
		     test_setitimer_real_old_value_disarm(), &failed);
	report_group("setitimer real repeating signal",
		     test_setitimer_real_repeating_signal(), &failed);
	report_group("setitimer error paths", test_setitimer_errors(),
		     &failed);
	report_group("timer_create sigev none",
		     test_timer_create_sigev_none(), &failed);
	report_group("timer_settime relative gettime",
		     test_timer_settime_relative_gettime(), &failed);
	report_group("timer_settime absolute gettime",
		     test_timer_settime_absolute_gettime(), &failed);
	report_group("timer default sigev signal",
		     test_timer_default_sigev_signal(), &failed);
	report_group("timer delete and getoverrun",
		     test_timer_delete_and_getoverrun(), &failed);
	report_group("timer interval overrun",
		     test_timer_interval_overrun(), &failed);
	report_group("timer exec deletes timer",
		     test_timer_exec_deletes_timer(), &failed);
	report_group("clock_settime error paths", test_clock_settime_errors(),
		     &failed);

	if (failed)
		printf("time_test: %d test group(s) FAILED\n", failed);
	else
		printf("time_test: all tests PASSED\n");

	return failed ? 1 : 0;
}

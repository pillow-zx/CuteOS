#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <utest.h>

static volatile sig_atomic_t time_signal_count;

static void time_signal_handler(int signal)
{
	(void)signal;
	time_signal_count++;
}

UT_CASE(time_clocks_and_realtime_limit, 1500)
{
	struct timespec before;
	struct timespec after;
	struct timespec resolution;

	UT_ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &before), 0);
	UT_ASSERT_EQ(nanosleep(&(struct timespec){.tv_nsec = 20000000}, NULL), 0);
	UT_ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &after), 0);
	UT_EXPECT((after.tv_sec > before.tv_sec) ||
		  (after.tv_sec == before.tv_sec && after.tv_nsec > before.tv_nsec));
	UT_ASSERT_EQ(clock_getres(CLOCK_MONOTONIC, &resolution), 0);
	UT_EXPECT(resolution.tv_sec > 0 || resolution.tv_nsec > 0);
	UT_EXPECT_ERRNO(clock_settime(CLOCK_REALTIME, &after), EPERM);
}

UT_CASE(time_nanosleep_interrupt_remainder, 5000)
{
	struct sigaction action = {
		.sa_handler = time_signal_handler,
	};
	struct sigaction old_action;
	struct timespec requested = {
		.tv_nsec = 200000000,
	};
	struct timespec remainder = {};
	pid_t child;

	UT_ASSERT_EQ(sigemptyset(&action.sa_mask), 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &action, &old_action), 0);
	time_signal_count = 0;
	child = UT_FORK();
	if (child == 0) {
		(void)nanosleep(&(struct timespec){.tv_nsec = 30000000}, NULL);
		(void)kill(getppid(), SIGUSR1);
		_exit(0);
	}
	UT_EXPECT_ERRNO(nanosleep(&requested, &remainder), EINTR);
	UT_EXPECT(time_signal_count > 0);
	UT_EXPECT(remainder.tv_sec > 0 || remainder.tv_nsec > 0);
	UT_EXPECT(remainder.tv_sec < requested.tv_sec ||
		  (remainder.tv_sec == requested.tv_sec &&
		   remainder.tv_nsec <= requested.tv_nsec));
	UT_EXPECT_EXIT(child, 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &old_action, NULL), 0);
}

UT_CASE(time_interval_posix_timer_and_timeout_wait, 5000)
{
	struct sigaction action = {
		.sa_handler = time_signal_handler,
	};
	struct sigaction old_action;
	struct itimerval interval = {
		.it_value.tv_usec = 30000,
	};
	struct sigevent event = {
		.sigev_notify = SIGEV_SIGNAL,
		.sigev_signo = SIGUSR2,
	};
	struct itimerspec deadline = {
		.it_value.tv_nsec = 30000000,
	};
	struct timespec wait_timeout = {
		.tv_sec = 1,
	};
	sigset_t set;
	sigset_t old_set;
	timer_t timer;

	UT_ASSERT_EQ(sigemptyset(&action.sa_mask), 0);
	UT_ASSERT_EQ(sigaction(SIGALRM, &action, &old_action), 0);
	time_signal_count = 0;
	UT_ASSERT_EQ(setitimer(ITIMER_REAL, &interval, NULL), 0);
	(void)nanosleep(&(struct timespec){.tv_nsec = 100000000}, NULL);
	UT_EXPECT(time_signal_count > 0);
	UT_ASSERT_EQ(setitimer(ITIMER_REAL, &(struct itimerval){0}, NULL), 0);
	UT_ASSERT_EQ(sigaction(SIGALRM, &old_action, NULL), 0);
	UT_ASSERT_EQ(sigemptyset(&set), 0);
	UT_ASSERT_EQ(sigaddset(&set, SIGUSR2), 0);
	UT_ASSERT_EQ(sigprocmask(SIG_BLOCK, &set, &old_set), 0);
	UT_ASSERT_EQ(timer_create(CLOCK_MONOTONIC, &event, &timer), 0);
	UT_ASSERT_EQ(timer_settime(timer, 0, &deadline, NULL), 0);
	UT_EXPECT_EQ(sigtimedwait(&set, NULL, &wait_timeout), SIGUSR2);
	UT_ASSERT_EQ(timer_delete(timer), 0);
	UT_EXPECT_ERRNO(sigtimedwait(&set, NULL,
					&(struct timespec){.tv_nsec = 1000000}), EAGAIN);
	UT_ASSERT_EQ(sigprocmask(SIG_SETMASK, &old_set, NULL), 0);
}

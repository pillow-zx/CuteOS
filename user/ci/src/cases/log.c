#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <utest.h>

enum {
	SYSLOG_ACTION_CLOSE = 0,
	SYSLOG_ACTION_OPEN = 1,
	SYSLOG_ACTION_READ = 2,
	SYSLOG_ACTION_READ_ALL = 3,
	SYSLOG_ACTION_READ_CLEAR = 4,
	SYSLOG_ACTION_CLEAR = 5,
	SYSLOG_ACTION_CONSOLE_LEVEL = 8,
	SYSLOG_ACTION_SIZE_UNREAD = 9,
	SYSLOG_ACTION_SIZE_BUFFER = 10,
};

static volatile sig_atomic_t log_signal_count;

static long log_klogctl(int type, char *buffer, int size)
{
	return syscall(SYS_syslog, type, buffer, size);
}

static void log_signal_handler(int signal)
{
	(void)signal;
	log_signal_count++;
}

static void log_run_dmesg(void)
{
	static const char script[] = "set -eu\n"
				     "dmesg -r > dmesg.raw\n"
				     "test -s dmesg.raw\n"
				     "grep -Eq '^<[0-7]>' dmesg.raw\n"
				     "dmesg -c > dmesg.clear\n"
				     "test -s dmesg.clear\n"
				     "dmesg > dmesg.after\n"
				     "test ! -s dmesg.after\n";
	char *const argv[] = {
		"sh",
		"dmesg-workload.sh",
		NULL,
	};
	char *const envp[] = {
		"PATH=/bin:/sbin",
		"HOME=/",
		NULL,
	};
	pid_t child;

	UT_ASSERT_EQ(ut_write_file("dmesg-workload.sh", script,
				   sizeof(script) - 1, 0700),
		     0);
	child = UT_FORK();
	if (child == 0) {
		execve("/bin/sh", argv, envp);
		_exit(127);
	}
	UT_EXPECT_EXIT(child, 0);
}

UT_CASE(log_syslog_ring_and_busybox_dmesg, 5000)
{
	struct sigaction action = {
		.sa_handler = log_signal_handler,
	};
	struct sigaction old_action;
	char buffer[128];
	long unread;
	pid_t child;

	UT_ASSERT_EQ(log_klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0), 4096);
	UT_EXPECT_ERRNO(log_klogctl(11, NULL, 0), EINVAL);
	UT_EXPECT_ERRNO(log_klogctl(SYSLOG_ACTION_READ_ALL, NULL, 1), EINVAL);
	UT_EXPECT_ERRNO(log_klogctl(SYSLOG_ACTION_READ_ALL, buffer, -1),
			EINVAL);
	UT_ASSERT_EQ(log_klogctl(SYSLOG_ACTION_CLOSE, NULL, 0), 0);
	UT_ASSERT_EQ(log_klogctl(SYSLOG_ACTION_OPEN, NULL, 0), 0);
	UT_EXPECT_ERRNO(log_klogctl(SYSLOG_ACTION_CONSOLE_LEVEL, NULL, 0),
			ENOSYS);

	child = UT_FORK();
	if (child == 0) {
		if (setuid(1000) < 0)
			_exit(125);
		if (log_klogctl(SYSLOG_ACTION_READ_ALL, buffer,
				sizeof(buffer)) < 0)
			_exit(1);
		if (log_klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0) != 4096)
			_exit(2);
		errno = 0;
		if (log_klogctl(SYSLOG_ACTION_READ, buffer, sizeof(buffer)) !=
			    -1 ||
		    errno != EPERM)
			_exit(3);
		errno = 0;
		if (log_klogctl(SYSLOG_ACTION_CLEAR, NULL, 0) != -1 ||
		    errno != EPERM)
			_exit(4);
		_exit(0);
	}
	UT_EXPECT_EXIT(child, 0);

	UT_ASSERT(log_klogctl(SYSLOG_ACTION_READ_ALL, buffer, sizeof(buffer)) >
		  0);
	unread = log_klogctl(SYSLOG_ACTION_SIZE_UNREAD, NULL, 0);
	UT_ASSERT(unread > 0);
	UT_EXPECT_ERRNO(log_klogctl(SYSLOG_ACTION_READ,
				    (char *)(uintptr_t)(UINTPTR_MAX - 7), 1),
			EFAULT);
	UT_EXPECT_EQ(log_klogctl(SYSLOG_ACTION_SIZE_UNREAD, NULL, 0), unread);
	UT_ASSERT_EQ(log_klogctl(SYSLOG_ACTION_READ, buffer, 1), 1);
	UT_EXPECT_EQ(log_klogctl(SYSLOG_ACTION_SIZE_UNREAD, NULL, 0),
		     unread - 1);

	log_run_dmesg();
	UT_ASSERT_EQ(
		log_klogctl(SYSLOG_ACTION_READ_ALL, buffer, sizeof(buffer)), 0);
	UT_ASSERT_EQ(log_klogctl(SYSLOG_ACTION_CLEAR, NULL, 0), 0);
	UT_ASSERT_EQ(
		log_klogctl(SYSLOG_ACTION_READ_ALL, buffer, sizeof(buffer)), 0);

	for (;;) {
		unread = log_klogctl(SYSLOG_ACTION_SIZE_UNREAD, NULL, 0);
		UT_ASSERT(unread >= 0);
		if (unread == 0)
			break;
		UT_ASSERT_EQ(log_klogctl(SYSLOG_ACTION_READ, buffer,
					 (int)(unread < (long)sizeof(buffer)
						       ? unread
						       : (long)sizeof(buffer))),
			     unread < (long)sizeof(buffer)
				     ? unread
				     : (long)sizeof(buffer));
	}

	UT_ASSERT_EQ(sigemptyset(&action.sa_mask), 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &action, &old_action), 0);
	log_signal_count = 0;
	child = UT_FORK();
	if (child == 0) {
		(void)nanosleep(&(struct timespec){.tv_nsec = 20000000}, NULL);
		(void)kill(getppid(), SIGUSR1);
		_exit(0);
	}
	UT_EXPECT_ERRNO(log_klogctl(SYSLOG_ACTION_READ, buffer, sizeof(buffer)),
			EINTR);
	UT_EXPECT(log_signal_count > 0);
	UT_EXPECT_EXIT(child, 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &old_action, NULL), 0);
}

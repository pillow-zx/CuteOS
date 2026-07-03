/*
 * user/bin/poll_test.c - ppoll readiness and wait tests
 */

#include <ulib.h>

static int expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int expect_revents(const char *name, short got, short mask, short want)
{
	if ((got & mask) != want) {
		printf("FAIL: %s expected revents mask 0x%x got 0x%x\n", name,
		       want, got);
		return 1;
	}

	return 0;
}

static int expect_fd_ready(const char *name, fd_set *set, int fd, int want)
{
	int got = FD_ISSET(fd, set) ? 1 : 0;

	if (got != want) {
		printf("FAIL: %s expected fd %d ready=%d got=%d\n", name, fd,
		       want, got);
		return 1;
	}

	return 0;
}

static int expect_epoll_events(const char *name, unsigned int got,
			       unsigned int mask, unsigned int want)
{
	if ((got & mask) != want) {
		printf("FAIL: %s expected events mask 0x%x got 0x%x\n", name,
		       want, got);
		return 1;
	}

	return 0;
}

static long epoll_pwait_raw(int epfd, struct epoll_event *events,
			    int maxevents, long timeout,
			    const unsigned long *sigmask, size_t sigsetsize)
{
	return syscall(SYS_epoll_pwait, epfd, (long)events, maxevents, timeout,
		       (long)sigmask, sigsetsize);
}

static int test_ready_pipe(void)
{
	struct pollfd pfd;
	int fds[2];
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: pipe ready\n");
		return 1;
	}
	write(fds[1], "x", 1);

	pfd.fd = fds[0];
	pfd.events = POLLIN;
	pfd.revents = 0;
	failed += expect_ret("ready pipe ppoll", ppoll(&pfd, 1, NULL, NULL), 1);
	failed += expect_revents("ready pipe revents", pfd.revents, POLLIN,
				 POLLIN);

	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_invalid_fd(void)
{
	struct timespec ts = {0, 0};
	struct pollfd pfd = {
		.fd = 30,
		.events = POLLIN,
		.revents = 0,
	};
	int failed = 0;

	failed += expect_ret("invalid fd ppoll", ppoll(&pfd, 1, &ts, NULL), 1);
	failed += expect_revents("invalid fd revents", pfd.revents, POLLNVAL,
				 POLLNVAL);
	return failed;
}

static int test_pipe_hup(void)
{
	struct pollfd pfd;
	int fds[2];
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: pipe hup\n");
		return 1;
	}
	close(fds[1]);

	pfd.fd = fds[0];
	pfd.events = POLLIN;
	pfd.revents = 0;
	failed += expect_ret("pipe hup ppoll", ppoll(&pfd, 1, NULL, NULL), 1);
	failed += expect_revents("pipe hup revents", pfd.revents, POLLHUP,
				 POLLHUP);

	close(fds[0]);
	return failed;
}

static int test_poll_normal_aliases(void)
{
	struct timespec ts = {0, 0};
	struct pollfd pfds[2];
	int fds[2];
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: poll normal aliases pipe\n");
		return 1;
	}

	write(fds[1], "a", 1);
	pfds[0].fd = fds[0];
	pfds[0].events = POLLRDNORM;
	pfds[0].revents = 0;
	pfds[1].fd = fds[1];
	pfds[1].events = POLLWRNORM;
	pfds[1].revents = 0;

	failed += expect_ret("ppoll normal aliases", ppoll(pfds, 2, &ts, NULL),
			     2);
	failed += expect_revents("ppoll rdnorm alias", pfds[0].revents,
				 POLLRDNORM, POLLRDNORM);
	failed += expect_revents("ppoll wrnorm alias", pfds[1].revents,
				 POLLWRNORM, POLLWRNORM);

	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_blocking_pipe_wakeup(void)
{
	struct pollfd pfd;
	struct timespec ts = {1, 0};
	unsigned long sigmask = 1UL << (SIGCHLD - 1);
	int fds[2];
	long pid;
	long waited;
	int status = 0;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: pipe wakeup\n");
		return 1;
	}

	pid = fork();
	if (pid < 0) {
		printf("FAIL: fork wakeup: %ld\n", pid);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (pid == 0) {
		struct timespec child_sleep = {0, 1000000};

		close(fds[0]);
		nanosleep(&child_sleep, NULL);
		write(fds[1], "y", 1);
		close(fds[1]);
		exit(0);
	}

	close(fds[1]);
	pfd.fd = fds[0];
	pfd.events = POLLIN;
	pfd.revents = 0;
	failed += expect_ret("blocking ppoll wakeup",
			     ppoll(&pfd, 1, &ts, &sigmask), 1);
	failed += expect_revents("blocking wakeup revents", pfd.revents, POLLIN,
				 POLLIN);
	close(fds[0]);

	waited = wait4(pid, &status, 0, NULL);
	if (waited != pid || status != 0) {
		printf("FAIL: wait wakeup child waited=%ld status=%d\n", waited,
		       status);
		failed++;
	}

	return failed;
}

static int test_timeout(void)
{
	struct timespec ts = {0, 1000000};
	struct pollfd pfd;
	int fds[2];
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: pipe timeout\n");
		return 1;
	}

	pfd.fd = fds[0];
	pfd.events = POLLIN;
	pfd.revents = 0;
	failed += expect_ret("timeout ppoll", ppoll(&pfd, 1, &ts, NULL), 0);
	failed += expect_revents("timeout revents", pfd.revents,
				 POLLIN | POLLHUP | POLLERR | POLLNVAL, 0);

	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_pselect_ready_pipe(void)
{
	struct timespec ts = {0, 0};
	fd_set readfds;
	int fds[2];
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: pselect ready pipe\n");
		return 1;
	}

	write(fds[1], "z", 1);
	FD_ZERO(&readfds);
	FD_SET(fds[0], &readfds);

	failed += expect_ret("ready pipe pselect6",
			     pselect6(fds[0] + 1, &readfds, NULL, NULL, &ts,
				      NULL),
			     1);
	failed += expect_fd_ready("ready pipe readfds", &readfds, fds[0], 1);

	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_pselect_invalid_fd(void)
{
	struct timespec ts = {0, 0};
	fd_set readfds;
	int failed = 0;

	FD_ZERO(&readfds);
	FD_SET(30, &readfds);

	failed += expect_ret("invalid fd pselect6",
			     pselect6(31, &readfds, NULL, NULL, &ts, NULL),
			     -EBADF);
	failed += expect_fd_ready("invalid fd set preserved", &readfds, 30, 1);
	return failed;
}

static int test_pselect_timeout(void)
{
	struct timespec ts = {0, 1000000};
	fd_set readfds;
	int fds[2];
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: pselect timeout pipe\n");
		return 1;
	}

	FD_ZERO(&readfds);
	FD_SET(fds[0], &readfds);

	failed += expect_ret("timeout pselect6",
			     pselect6(fds[0] + 1, &readfds, NULL, NULL, &ts,
				      NULL),
			     0);
	failed += expect_fd_ready("timeout readfds", &readfds, fds[0], 0);

	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_pselect_blocking_pipe_wakeup(void)
{
	struct timespec ts = {1, 0};
	unsigned long sigmask = 1UL << (SIGCHLD - 1);
	unsigned long blocked = 0;
	unsigned long observed = ~0UL;
	fd_set readfds;
	int fds[2];
	long pid;
	long waited;
	int status = 0;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: pselect wakeup pipe\n");
		return 1;
	}
	if (sigprocmask(SIG_SETMASK, &blocked, NULL) != 0) {
		printf("FAIL: pselect wakeup sigprocmask init\n");
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	pid = fork();
	if (pid < 0) {
		printf("FAIL: pselect wakeup fork: %ld\n", pid);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (pid == 0) {
		struct timespec child_sleep = {0, 1000000};

		close(fds[0]);
		nanosleep(&child_sleep, NULL);
		write(fds[1], "p", 1);
		close(fds[1]);
		exit(0);
	}

	close(fds[1]);
	FD_ZERO(&readfds);
	FD_SET(fds[0], &readfds);

	failed += expect_ret("blocking pselect6 wakeup",
			     pselect6(fds[0] + 1, &readfds, NULL, NULL, &ts,
				      &sigmask),
			     1);
	failed += expect_fd_ready("blocking pselect readfds", &readfds, fds[0],
				  1);
	failed += expect_ret("pselect mask observe",
			     sigprocmask(0, NULL, &observed), 0);
	failed += expect_ret("pselect mask restored", (long)observed, 0);
	close(fds[0]);

	waited = wait4(pid, &status, 0, NULL);
	if (waited != pid || status != 0) {
		printf("FAIL: pselect wakeup child waited=%ld status=%d\n",
		       waited, status);
		failed++;
	}

	return failed;
}

static int test_epoll_create1_basic(void)
{
	long epfd;
	int failed = 0;

	epfd = epoll_create1(0);
	failed += expect_ret("epoll_create1 basic", epfd, 3);
	if (epfd >= 0)
		close((int)epfd);

	return failed;
}

static int test_epoll_create1_cloexec(void)
{
	long epfd;
	int failed = 0;

	epfd = epoll_create1(EPOLL_CLOEXEC);
	failed += expect_ret("epoll_create1 cloexec create", epfd, 3);
	if (epfd >= 0) {
		failed += expect_ret("epoll_create1 cloexec flag",
				     fcntl((int)epfd, F_GETFD, 0),
				     FD_CLOEXEC);
		close((int)epfd);
	}

	return failed;
}

static int test_epoll_create1_invalid_flags(void)
{
	return expect_ret("epoll_create1 invalid flags",
			  epoll_create1(EPOLL_CLOEXEC | 0x1), -EINVAL);
}

static int test_epoll_ctl_add_pipe(void)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data = 0x1234,
	};
	int fds[2];
	long epfd;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll_ctl add pipe\n");
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_ctl add create epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	failed += expect_ret("epoll_ctl add pipe",
			     epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &ev),
			     0);

	close((int)epfd);
	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_epoll_ctl_duplicate_add(void)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data = 1,
	};
	int fds[2];
	long epfd;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll_ctl duplicate add pipe\n");
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_ctl duplicate add epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	failed += expect_ret("epoll_ctl duplicate add first",
			     epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &ev),
			     0);
	failed += expect_ret("epoll_ctl duplicate add second",
			     epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &ev),
			     -EEXIST);

	close((int)epfd);
	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_epoll_ctl_mod_del(void)
{
	struct epoll_event add_ev = {
		.events = EPOLLIN,
		.data = 1,
	};
	struct epoll_event mod_ev = {
		.events = EPOLLOUT | EPOLLET | EPOLLONESHOT,
		.data = 2,
	};
	int fds[2];
	long epfd;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll_ctl mod del pipe\n");
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_ctl mod del epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	failed += expect_ret("epoll_ctl mod del add",
			     epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0],
				       &add_ev),
			     0);
	failed += expect_ret("epoll_ctl mod existing",
			     epoll_ctl((int)epfd, EPOLL_CTL_MOD, fds[0],
				       &mod_ev),
			     0);
	failed += expect_ret("epoll_ctl del existing",
			     epoll_ctl((int)epfd, EPOLL_CTL_DEL, fds[0], NULL),
			     0);
	failed += expect_ret("epoll_ctl del missing",
			     epoll_ctl((int)epfd, EPOLL_CTL_DEL, fds[0], NULL),
			     -ENOENT);
	failed += expect_ret("epoll_ctl mod missing",
			     epoll_ctl((int)epfd, EPOLL_CTL_MOD, fds[0],
				       &mod_ev),
			     -ENOENT);

	close((int)epfd);
	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_epoll_ctl_invalid_args(void)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data = 3,
	};
	struct epoll_event bad_ev = {
		.events = EPOLLIN | EPOLLEXCLUSIVE,
		.data = 4,
	};
	int fds[2];
	long epfd;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll_ctl invalid args pipe\n");
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_ctl invalid args epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	failed += expect_ret("epoll_ctl invalid op",
			     epoll_ctl((int)epfd, 99, fds[0], &ev), -EINVAL);
	failed += expect_ret("epoll_ctl self target",
			     epoll_ctl((int)epfd, EPOLL_CTL_ADD, (int)epfd,
				       &ev),
			     -EINVAL);
	failed += expect_ret("epoll_ctl null add event",
			     epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], NULL),
			     -EFAULT);
	failed += expect_ret("epoll_ctl null mod event",
			     epoll_ctl((int)epfd, EPOLL_CTL_MOD, fds[0], NULL),
			     -EFAULT);
	failed += expect_ret("epoll_ctl invalid mask",
			     epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0],
				       &bad_ev),
			     -EINVAL);

	close((int)epfd);
	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_epoll_ctl_invalid_targets(void)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data = 5,
	};
	long epfd;
	long nested;
	long dirfd;
	int failed = 0;

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_ctl invalid targets epoll %ld\n", epfd);
		return 1;
	}

	dirfd = openat(AT_FDCWD, "/", O_RDONLY | O_DIRECTORY, 0);
	if (dirfd < 0) {
		printf("FAIL: epoll_ctl invalid targets dir %ld\n", dirfd);
		close((int)epfd);
		return 1;
	}

	nested = epoll_create1(0);
	if (nested < 0) {
		printf("FAIL: epoll_ctl invalid targets nested %ld\n", nested);
		close((int)dirfd);
		close((int)epfd);
		return 1;
	}

	failed += expect_ret("epoll_ctl directory target",
			     epoll_ctl((int)epfd, EPOLL_CTL_ADD, (int)dirfd,
				       &ev),
			     -EPERM);
	failed += expect_ret("epoll_ctl nested epoll target",
			     epoll_ctl((int)epfd, EPOLL_CTL_ADD, (int)nested,
				       &ev),
			     -EINVAL);

	close((int)nested);
	close((int)dirfd);
	close((int)epfd);
	return failed;
}

static int test_epoll_pwait_ready_pipe(void)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data = 0x55aa,
	};
	struct epoll_event out = {
		.events = 0,
		.data = 0,
	};
	int fds[2];
	long epfd;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll_pwait ready pipe\n");
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_pwait ready create epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	if (epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &ev) != 0) {
		printf("FAIL: epoll_pwait ready add pipe\n");
		close((int)epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	write(fds[1], "e", 1);

	failed += expect_ret("epoll_pwait ready pipe",
			     epoll_pwait((int)epfd, &out, 1, 0, NULL), 1);
	failed += expect_epoll_events("epoll_pwait ready events", out.events,
				      EPOLLIN, EPOLLIN);
	failed += expect_ret("epoll_pwait ready data",
			     (long)out.data, (long)ev.data);

	close((int)epfd);
	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_epoll_pwait_timeout(void)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data = 7,
	};
	struct epoll_event out = {
		.events = 0,
		.data = 0,
	};
	int fds[2];
	long epfd;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll_pwait timeout pipe\n");
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_pwait timeout create epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &ev) != 0) {
		printf("FAIL: epoll_pwait timeout add pipe\n");
		close((int)epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	failed += expect_ret("epoll_pwait timeout zero",
			     epoll_pwait((int)epfd, &out, 1, 0, NULL), 0);
	failed += expect_ret("epoll_pwait timeout positive",
			     epoll_pwait((int)epfd, &out, 1, 1, NULL), 0);

	close((int)epfd);
	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_epoll_pwait_blocking_wakeup(void)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data = 0x88,
	};
	struct epoll_event out = {
		.events = 0,
		.data = 0,
	};
	struct timespec child_sleep = {0, 1000000};
	unsigned long sigmask = 1UL << (SIGCHLD - 1);
	unsigned long blocked = 0;
	unsigned long observed = ~0UL;
	int fds[2];
	long epfd;
	long pid;
	long waited;
	int status = 0;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll_pwait wakeup pipe\n");
		return 1;
	}
	if (sigprocmask(SIG_SETMASK, &blocked, NULL) != 0) {
		printf("FAIL: epoll_pwait wakeup sigprocmask init\n");
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_pwait wakeup create epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &ev) != 0) {
		printf("FAIL: epoll_pwait wakeup add pipe\n");
		close((int)epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	pid = fork();
	if (pid < 0) {
		printf("FAIL: epoll_pwait wakeup fork %ld\n", pid);
		close((int)epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (pid == 0) {
		close(fds[0]);
		nanosleep(&child_sleep, NULL);
		write(fds[1], "w", 1);
		close(fds[1]);
		exit(0);
	}

	close(fds[1]);
	failed += expect_ret("epoll_pwait blocking wakeup",
			     epoll_pwait((int)epfd, &out, 1, 1000, &sigmask),
			     1);
	failed += expect_epoll_events("epoll_pwait blocking events", out.events,
				      EPOLLIN, EPOLLIN);
	failed += expect_ret("epoll_pwait blocking data",
			     (long)out.data, (long)ev.data);
	failed += expect_ret("epoll_pwait mask observe",
			     sigprocmask(0, NULL, &observed), 0);
	failed += expect_ret("epoll_pwait mask restored", (long)observed, 0);

	close(fds[0]);
	close((int)epfd);
	waited = wait4(pid, &status, 0, NULL);
	if (waited != pid || status != 0) {
		printf("FAIL: epoll_pwait wakeup child waited=%ld status=%d\n",
		       waited, status);
		failed++;
	}

	return failed;
}

static int test_epoll_pwait_negative_timeout(void)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data = 0x99,
	};
	struct epoll_event out = {
		.events = 0,
		.data = 0,
	};
	struct timespec child_sleep = {0, 1000000};
	unsigned long sigmask = 1UL << (SIGCHLD - 1);
	int fds[2];
	long epfd;
	long pid;
	long waited;
	int status = 0;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll_pwait negative timeout pipe\n");
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_pwait negative timeout epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &ev) != 0) {
		printf("FAIL: epoll_pwait negative timeout add pipe\n");
		close((int)epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	pid = fork();
	if (pid < 0) {
		printf("FAIL: epoll_pwait negative timeout fork %ld\n", pid);
		close((int)epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (pid == 0) {
		close(fds[0]);
		nanosleep(&child_sleep, NULL);
		write(fds[1], "n", 1);
		close(fds[1]);
		exit(0);
	}

	close(fds[1]);
	failed += expect_ret("epoll_pwait negative timeout",
			     epoll_pwait((int)epfd, &out, 1, -2, &sigmask), 1);
	failed += expect_epoll_events("epoll_pwait negative events", out.events,
				      EPOLLIN, EPOLLIN);

	close(fds[0]);
	close((int)epfd);
	waited = wait4(pid, &status, 0, NULL);
	if (waited != pid || status != 0) {
		printf("FAIL: epoll_pwait negative child waited=%ld status=%d\n",
		       waited, status);
		failed++;
	}

	return failed;
}

static int test_epoll_pwait_pipe_hup(void)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data = 0x11,
	};
	struct epoll_event out = {
		.events = 0,
		.data = 0,
	};
	int fds[2];
	long epfd;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll_pwait hup pipe\n");
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_pwait hup epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &ev) != 0) {
		printf("FAIL: epoll_pwait hup add pipe\n");
		close((int)epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	close(fds[1]);
	failed += expect_ret("epoll_pwait pipe hup",
			     epoll_pwait((int)epfd, &out, 1, 0, NULL), 1);
	failed += expect_epoll_events("epoll_pwait hup events", out.events,
				      EPOLLHUP, EPOLLHUP);

	close((int)epfd);
	close(fds[0]);
	return failed;
}

static int test_epoll_pwait_normal_aliases(void)
{
	struct epoll_event ev = {
		.events = EPOLLRDNORM,
		.data = 0x41,
	};
	struct epoll_event out = {
		.events = 0,
		.data = 0,
	};
	int fds[2];
	long epfd;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll normal aliases pipe\n");
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll normal aliases epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &ev) != 0) {
		printf("FAIL: epoll normal aliases add pipe\n");
		close((int)epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	write(fds[1], "r", 1);
	failed += expect_ret("epoll_pwait normal alias",
			     epoll_pwait((int)epfd, &out, 1, 0, NULL), 1);
	failed += expect_epoll_events("epoll_pwait rdnorm alias", out.events,
				      EPOLLRDNORM, EPOLLRDNORM);
	failed += expect_ret("epoll_pwait normal alias data", (long)out.data,
			     (long)ev.data);

	close((int)epfd);
	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_epoll_pwait_invalid_args(void)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data = 0x21,
	};
	struct epoll_event out = {
		.events = 0,
		.data = 0,
	};
	int fds[2];
	long epfd;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll_pwait invalid args pipe\n");
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_pwait invalid args epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &ev) != 0) {
		printf("FAIL: epoll_pwait invalid args add pipe\n");
		close((int)epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	failed += expect_ret("epoll_pwait zero maxevents",
			     epoll_pwait((int)epfd, &out, 0, 0, NULL), -EINVAL);
	failed += expect_ret("epoll_pwait null events",
			     epoll_pwait((int)epfd, NULL, 1, 0, NULL), -EFAULT);
	failed += expect_ret("epoll_pwait raw bad sigsetsize",
			     epoll_pwait_raw((int)epfd, &out, 1, 0, NULL, 1),
			     -EINVAL);
	failed += expect_ret("epoll_pwait non-epoll fd",
			     epoll_pwait(fds[0], &out, 1, 0, NULL), -EINVAL);

	close((int)epfd);
	close(fds[0]);
	close(fds[1]);
	return failed;
}

static int test_epoll_pwait_unsupported_flags(void)
{
	struct epoll_event et_ev = {
		.events = EPOLLIN | EPOLLET,
		.data = 0x31,
	};
	struct epoll_event one_ev = {
		.events = EPOLLIN | EPOLLONESHOT,
		.data = 0x32,
	};
	struct epoll_event out = {
		.events = 0,
		.data = 0,
	};
	int fds[2];
	long epfd;
	int failed = 0;

	if (pipe(fds) != 0) {
		printf("FAIL: epoll_pwait unsupported flags pipe\n");
		return 1;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		printf("FAIL: epoll_pwait unsupported flags epoll %ld\n", epfd);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	failed += expect_ret("epoll_pwait add et",
			     epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &et_ev),
			     0);
	failed += expect_ret("epoll_pwait reject et",
			     epoll_pwait((int)epfd, &out, 1, 0, NULL), -EINVAL);
	failed += expect_ret("epoll_pwait del et",
			     epoll_ctl((int)epfd, EPOLL_CTL_DEL, fds[0], NULL),
			     0);
	failed += expect_ret("epoll_pwait add oneshot",
			     epoll_ctl((int)epfd, EPOLL_CTL_ADD, fds[0], &one_ev),
			     0);
	failed += expect_ret("epoll_pwait reject oneshot",
			     epoll_pwait((int)epfd, &out, 1, 0, NULL), -EINVAL);

	close((int)epfd);
	close(fds[0]);
	close(fds[1]);
	return failed;
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("poll_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(void)
{
	int failed = 0;

	report_group("ready pipe", test_ready_pipe(), &failed);
	report_group("invalid fd", test_invalid_fd(), &failed);
	report_group("pipe hup", test_pipe_hup(), &failed);
	report_group("poll normal aliases", test_poll_normal_aliases(),
		     &failed);
	report_group("blocking pipe wakeup", test_blocking_pipe_wakeup(),
		     &failed);
	report_group("timeout", test_timeout(), &failed);
	report_group("pselect ready pipe", test_pselect_ready_pipe(), &failed);
	report_group("pselect invalid fd", test_pselect_invalid_fd(), &failed);
	report_group("pselect timeout", test_pselect_timeout(), &failed);
	report_group("pselect blocking pipe wakeup",
		     test_pselect_blocking_pipe_wakeup(), &failed);
	report_group("epoll_create1 basic", test_epoll_create1_basic(),
		     &failed);
	report_group("epoll_create1 cloexec", test_epoll_create1_cloexec(),
		     &failed);
	report_group("epoll_create1 invalid flags",
		     test_epoll_create1_invalid_flags(), &failed);
	report_group("epoll_ctl add pipe", test_epoll_ctl_add_pipe(), &failed);
	report_group("epoll_ctl duplicate add",
		     test_epoll_ctl_duplicate_add(), &failed);
	report_group("epoll_ctl mod del", test_epoll_ctl_mod_del(), &failed);
	report_group("epoll_ctl invalid args",
		     test_epoll_ctl_invalid_args(), &failed);
	report_group("epoll_ctl invalid targets",
		     test_epoll_ctl_invalid_targets(), &failed);
	report_group("epoll_pwait ready pipe", test_epoll_pwait_ready_pipe(),
		     &failed);
	report_group("epoll_pwait timeout", test_epoll_pwait_timeout(),
		     &failed);
	report_group("epoll_pwait blocking wakeup",
		     test_epoll_pwait_blocking_wakeup(), &failed);
	report_group("epoll_pwait negative timeout",
		     test_epoll_pwait_negative_timeout(), &failed);
	report_group("epoll_pwait pipe hup", test_epoll_pwait_pipe_hup(),
		     &failed);
	report_group("epoll_pwait normal aliases",
		     test_epoll_pwait_normal_aliases(), &failed);
	report_group("epoll_pwait invalid args",
		     test_epoll_pwait_invalid_args(), &failed);
	report_group("epoll_pwait unsupported flags",
		     test_epoll_pwait_unsupported_flags(), &failed);

	if (failed)
		printf("poll_test: %d test group(s) FAILED\n", failed);
	else
		printf("poll_test: all tests passed\n");
	return failed ? 1 : 0;
}

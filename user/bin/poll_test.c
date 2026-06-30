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
	report_group("blocking pipe wakeup", test_blocking_pipe_wakeup(),
		     &failed);
	report_group("timeout", test_timeout(), &failed);

	if (failed)
		printf("poll_test: %d test group(s) FAILED\n", failed);
	else
		printf("poll_test: all tests passed\n");
	return failed ? 1 : 0;
}

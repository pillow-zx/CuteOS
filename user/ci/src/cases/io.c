#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include <utest.h>

static volatile sig_atomic_t io_interrupted;

static void io_signal_handler(int signal)
{
	(void)signal;
	io_interrupted = 1;
}

UT_CASE(io_pipe_vector_and_offset, 1500)
{
	char first[4] = {};
	char second[8] = {};
	char read_buffer[8] = {};
	struct iovec write_iov[] = {
		{.iov_base = "abc", .iov_len = 3},
		{.iov_base = "def", .iov_len = 3},
	};
	struct iovec read_iov[] = {
		{.iov_base = first, .iov_len = 3},
		{.iov_base = second, .iov_len = 3},
	};
	int pipefd[2];
	int fd;

	UT_ASSERT_EQ(pipe(pipefd), 0);
	UT_ASSERT_EQ(writev(pipefd[1], write_iov, 2), 6);
	UT_ASSERT_EQ(readv(pipefd[0], read_iov, 2), 6);
	UT_EXPECT_STREQ(first, "abc");
	UT_EXPECT_STREQ(second, "def");
	UT_ASSERT_EQ(close(pipefd[0]), 0);
	UT_ASSERT_EQ(close(pipefd[1]), 0);
	UT_ASSERT_EQ(ut_write_file("offset", "abcdef", 6, 0600), 0);
	fd = open("offset", O_RDWR);
	UT_ASSERT(fd >= 0);
	UT_ASSERT_EQ(pwrite(fd, "XY", 2, 2), 2);
	UT_ASSERT_EQ(pread(fd, read_buffer, 6, 0), 6);
	UT_EXPECT_MEMEQ(read_buffer, "abXYef", 6);
	UT_EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 0);
	UT_ASSERT_EQ(close(fd), 0);
}

UT_CASE(io_nonblocking_and_sigpipe, 1500)
{
	int pipefd[2];
	pid_t child;

	UT_ASSERT_EQ(pipe2(pipefd, O_NONBLOCK), 0);
	UT_EXPECT_ERRNO(read(pipefd[0], &(char){0}, 1), EAGAIN);
	UT_ASSERT_EQ(close(pipefd[0]), 0);
	UT_ASSERT_EQ(close(pipefd[1]), 0);
	UT_ASSERT_EQ(pipe(pipefd), 0);
	child = UT_FORK();
	if (child == 0) {
		(void)close(pipefd[0]);
		(void)write(pipefd[1], "x", 1);
		_exit(127);
	}
	UT_ASSERT_EQ(close(pipefd[0]), 0);
	UT_ASSERT_EQ(close(pipefd[1]), 0);
	UT_EXPECT_SIGNAL(child, SIGPIPE);
}

UT_CASE(io_sendfile_and_splice, 1500)
{
	char *content;
	int input;
	int output;
	int pipefd[2];

	UT_ASSERT_EQ(ut_write_file("input", "sendfile-data", 13, 0600), 0);
	input = open("input", O_RDONLY);
	output = open("output", O_WRONLY | O_CREAT | O_TRUNC, 0600);
	UT_ASSERT(input >= 0);
	UT_ASSERT(output >= 0);
	UT_ASSERT_EQ(sendfile(output, input, NULL, 13), 13);
	UT_ASSERT_EQ(close(input), 0);
	UT_ASSERT_EQ(close(output), 0);
	content = ut_read_file("output", NULL);
	UT_ASSERT(content != NULL);
	UT_EXPECT_STREQ(content, "sendfile-data");
	free(content);
	UT_ASSERT_EQ(pipe(pipefd), 0);
	UT_ASSERT_EQ(write(pipefd[1], "splice-data", 11), 11);
	output = open("spliced", O_WRONLY | O_CREAT | O_TRUNC, 0600);
	UT_ASSERT(output >= 0);
	UT_ASSERT_EQ(splice(pipefd[0], NULL, output, NULL, 11, 0), 11);
	UT_ASSERT_EQ(close(pipefd[0]), 0);
	UT_ASSERT_EQ(close(pipefd[1]), 0);
	UT_ASSERT_EQ(close(output), 0);
	content = ut_read_file("spliced", NULL);
	UT_ASSERT(content != NULL);
	UT_EXPECT_STREQ(content, "splice-data");
	free(content);
	input = open("input", O_RDONLY);
	output = open("output", O_WRONLY);
	UT_ASSERT(input >= 0);
	UT_ASSERT(output >= 0);
	UT_EXPECT_ERRNO(splice(input, NULL, output, NULL, 1, 0), EINVAL);
	UT_ASSERT_EQ(close(input), 0);
	UT_ASSERT_EQ(close(output), 0);
}

UT_CASE(io_poll_select_epoll_and_eintr, 5000)
{
	struct sigaction action = {
		.sa_handler = io_signal_handler,
	};
	struct sigaction old_action;
	struct pollfd pollfd;
	struct epoll_event event = {
		.events = EPOLLIN,
		.data.u32 = 17,
	};
	struct epoll_event received = {};
	struct timespec timeout = {
		.tv_nsec = 500000000,
	};
	fd_set readfds;
	int pipefd[2];
	int epollfd;
	pid_t child;

	UT_ASSERT_EQ(pipe(pipefd), 0);
	pollfd = (struct pollfd){
		.fd = pipefd[0],
		.events = POLLIN,
	};
	UT_EXPECT_EQ(poll(&pollfd, 1, 0), 0);
	UT_ASSERT_EQ(write(pipefd[1], "p", 1), 1);
	UT_ASSERT_EQ(poll(&pollfd, 1, 0), 1);
	UT_EXPECT(pollfd.revents & POLLIN);
	FD_ZERO(&readfds);
	FD_SET(pipefd[0], &readfds);
	UT_ASSERT_EQ(pselect(pipefd[0] + 1, &readfds, NULL, NULL,
			     &(struct timespec){0}, NULL), 1);
	UT_EXPECT(FD_ISSET(pipefd[0], &readfds));
	epollfd = epoll_create1(EPOLL_CLOEXEC);
	UT_ASSERT(epollfd >= 0);
	UT_ASSERT_EQ(epoll_ctl(epollfd, EPOLL_CTL_ADD, pipefd[0], &event), 0);
	UT_ASSERT_EQ(epoll_pwait(epollfd, &received, 1, 0, NULL), 1);
	UT_EXPECT_EQ(received.data.u32, 17);
	UT_ASSERT_EQ(read(pipefd[0], &(char){0}, 1), 1);
	UT_ASSERT_EQ(sigemptyset(&action.sa_mask), 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &action, &old_action), 0);
	io_interrupted = 0;
	child = UT_FORK();
	if (child == 0) {
		(void)nanosleep(&(struct timespec){.tv_nsec = 50000000}, NULL);
		(void)kill(getppid(), SIGUSR1);
		_exit(0);
	}
	pollfd.revents = 0;
	UT_EXPECT_ERRNO(ppoll(&pollfd, 1, &timeout, NULL), EINTR);
	UT_EXPECT(io_interrupted);
	UT_EXPECT_EXIT(child, 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &old_action, NULL), 0);
	UT_ASSERT_EQ(close(epollfd), 0);
	UT_ASSERT_EQ(close(pipefd[0]), 0);
	UT_ASSERT_EQ(close(pipefd[1]), 0);
}

/*
 * user/bin/pipe_test.c - pipe behavior regression tests
 */

#include <ulib.h>

#define EPIPE 32

static int test_pipe_eof(void)
{
	int fds[2];
	char buf[8];
	long ret;
	int i;

	if (pipe(fds) != 0) {
		printf("FAIL: pipe\n");
		return 1;
	}

	ret = write(fds[1], "hello", 5);
	if (ret != 5) {
		printf("FAIL: write expected 5 got %ld\n", ret);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	close(fds[1]);

	ret = read(fds[0], buf, 5);
	if (ret != 5) {
		printf("FAIL: read data ret=%ld\n", ret);
		close(fds[0]);
		return 1;
	}
	for (i = 0; i < 5; i++) {
		if (buf[i] != "hello"[i]) {
			printf("FAIL: read byte %d\n", i);
			close(fds[0]);
			return 1;
		}
	}

	ret = read(fds[0], buf, 1);
	if (ret != 0) {
		printf("FAIL: eof expected 0 got %ld\n", ret);
		close(fds[0]);
		return 1;
	}

	close(fds[0]);
	return 0;
}

static int test_pipe_epipe(void)
{
	struct sigaction ign;
	struct sigaction dfl;
	int fds[2];
	long ret;

	memset(&ign, 0, sizeof(ign));
	ign.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &ign, NULL);

	if (pipe(fds) != 0) {
		printf("FAIL: pipe epipe\n");
		return 1;
	}

	close(fds[0]);
	ret = write(fds[1], "x", 1);
	close(fds[1]);

	memset(&dfl, 0, sizeof(dfl));
	dfl.sa_handler = SIG_DFL;
	sigaction(SIGPIPE, &dfl, NULL);

	if (ret != -EPIPE) {
		printf("FAIL: epipe expected -%d got %ld\n", EPIPE, ret);
		return 1;
	}

	return 0;
}

int main(void)
{
	if (test_pipe_eof())
		return 1;
	if (test_pipe_epipe())
		return 1;

	printf("pipe_test: ok\n");
	return 0;
}

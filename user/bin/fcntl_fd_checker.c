/*
 * user/bin/fcntl_fd_checker.c - exec-side FD_CLOEXEC checker
 */

#include <ulib.h>

int main(int argc, char **argv)
{
	int fd;
	long ret;

	if (argc != 2) {
		printf("fcntl_fd_checker: expected fd argument\n");
		return 2;
	}

	fd = atoi(argv[1]);
	ret = fcntl(fd, F_GETFD, 0);
	if (ret != -EBADF) {
		printf("fcntl_fd_checker: fd %d expected -EBADF got %ld\n", fd,
		       ret);
		return 1;
	}

	return 0;
}

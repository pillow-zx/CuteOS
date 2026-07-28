#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	char *end;
	long fd;

	if (argc != 2)
		return 2;
	errno = 0;
	fd = strtol(argv[1], &end, 10);
	if (errno || *end || fd < 0 || fd > INT_MAX)
		return 3;
	errno = 0;
	if (fcntl((int)fd, F_GETFD) != -1 || errno != EBADF)
		return 4;
	return 0;
}

#include <ulib.h>

static int open_input(const char *path)
{
	if (streq(path, "-"))
		return 0;
	return (int)open(path, O_RDONLY);
}

int main(int argc, char **argv)
{
	int silent = 0;
	int first = 1;
	int fd1;
	int fd2;
	unsigned long byte = 1;
	unsigned long line = 1;
	char a[256];
	char b[256];
	long na = 0;
	long nb = 0;
	long ia = 0;
	long ib = 0;

	if (argc > 1 && streq(argv[1], "-s")) {
		silent = 1;
		first = 2;
	}
	if (argc - first != 2) {
		printf("usage: cmp [-s] FILE1 FILE2\n");
		return 2;
	}

	fd1 = open_input(argv[first]);
	if (fd1 < 0) {
		if (!silent)
			printf("cmp: %s: %s\n", argv[first], strerror(fd1));
		return 2;
	}
	fd2 = open_input(argv[first + 1]);
	if (fd2 < 0) {
		if (!silent)
			printf("cmp: %s: %s\n", argv[first + 1],
			       strerror(fd2));
		if (fd1 != 0)
			close(fd1);
		return 2;
	}

	while (1) {
		if (ia >= na) {
			na = read(fd1, a, sizeof(a));
			ia = 0;
			if (na < 0)
				goto error;
		}
		if (ib >= nb) {
			nb = read(fd2, b, sizeof(b));
			ib = 0;
			if (nb < 0)
				goto error;
		}
		if (na == 0 || nb == 0) {
			if (na == nb)
				break;
			if (!silent)
				printf("cmp: EOF on %s after byte %lu\n",
				       na == 0 ? argv[first] :
						  argv[first + 1],
				       byte - 1);
			if (fd1 != 0)
				close(fd1);
			if (fd2 != 0)
				close(fd2);
			return 1;
		}
		if (a[ia] != b[ib]) {
			if (!silent)
				printf("%s %s differ: byte %lu, line %lu\n",
				       argv[first], argv[first + 1], byte,
				       line);
			if (fd1 != 0)
				close(fd1);
			if (fd2 != 0)
				close(fd2);
			return 1;
		}
		if (a[ia] == '\n')
			line++;
		ia++;
		ib++;
		byte++;
	}

	if (fd1 != 0)
		close(fd1);
	if (fd2 != 0)
		close(fd2);
	return 0;

error:
	if (!silent)
		printf("cmp: read error\n");
	if (fd1 != 0)
		close(fd1);
	if (fd2 != 0)
		close(fd2);
	return 2;
}

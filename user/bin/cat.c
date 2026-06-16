#include <ulib.h>

static int cat_fd(int fd, const char *name)
{
	char buf[256];

	while (1) {
		long n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			printf("cat: %s: error %ld\n", name, (long)n);
			return 1;
		}
		if (n == 0)
			return 0;
		if (write(1, buf, (size_t)n) != n) {
			printf("cat: %s: error %ld\n", name, -1L);
			return 1;
		}
	}
}

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc == 1)
		return cat_fd(0, NULL);

	for (int i = 1; i < argc; i++) {
		if (streq(argv[i], "-")) {
			if (cat_fd(0, "-") != 0)
				failed = 1;
			continue;
		}
		long fd = open(argv[i], O_RDONLY);

		if (fd < 0) {
			printf("cat: %s: error %ld\n", argv[i], fd);
			failed = 1;
			continue;
		}
		if (cat_fd((int)fd, argv[i]) != 0)
			failed = 1;
		close((int)fd);
	}
	return failed;
}

#include <ulib.h>

#define TEE_MAX_FILES 16

int main(int argc, char **argv)
{
	int append = 0;
	int first = 1;
	int fds[TEE_MAX_FILES];
	int nfds = 0;
	int failed = 0;
	char buf[256];

	while (first < argc && argv[first][0] == '-' &&
	       argv[first][1] != '\0') {
		if (streq(argv[first], "--")) {
			first++;
			break;
		}
		if (streq(argv[first], "-a")) {
			append = 1;
			first++;
			continue;
		}
		printf("usage: tee [-a] [FILE...]\n");
		return 1;
	}

	for (int i = first; i < argc && nfds < TEE_MAX_FILES; i++) {
		long fd = openat(AT_FDCWD, argv[i],
				 O_CREAT | O_WRONLY |
					 (append ? O_APPEND : O_TRUNC),
				 0666);

		if (fd < 0) {
			printf("tee: %s: %s\n", argv[i], strerror(fd));
			failed = 1;
			continue;
		}
		fds[nfds++] = (int)fd;
	}
	if (argc - first > TEE_MAX_FILES)
		printf("tee: too many files, ignoring extras\n");

	while (1) {
		long n = read(0, buf, sizeof(buf));

		if (n < 0) {
			printf("tee: stdin: %s\n", strerror(n));
			failed = 1;
			break;
		}
		if (n == 0)
			break;
		if (write(1, buf, (size_t)n) != n)
			failed = 1;
		for (int i = 0; i < nfds; i++) {
			if (write(fds[i], buf, (size_t)n) != n)
				failed = 1;
		}
	}

	for (int i = 0; i < nfds; i++)
		close(fds[i]);
	return failed;
}

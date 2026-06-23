#include <ulib.h>

static int parse_count(const char *s, long *count)
{
	long n;

	if (!s || !*s)
		return -1;
	n = atoi(s);
	if (n < 0)
		return -1;
	*count = n;
	return 0;
}

static int head_fd(int fd, const char *name, long max_lines)
{
	char buf[256];
	long lines = 0;

	while (max_lines < 0 || lines < max_lines) {
		long n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			printf("head: %s: %s\n", name ? name : "-",
			       strerror(n));
			return 1;
		}
		if (n == 0)
			break;

		size_t out = 0;
		while (out < (size_t)n) {
			size_t chunk = out;

			while (chunk < (size_t)n &&
			       (max_lines < 0 || lines < max_lines)) {
				if (buf[chunk++] == '\n')
					lines++;
			}
			if (write(1, buf + out, chunk - out) !=
			    (long)(chunk - out))
				return 1;
			out = chunk;
			if (max_lines >= 0 && lines >= max_lines)
				break;
		}
	}

	return 0;
}

static int head_path(const char *path, long max_lines)
{
	long fd = open(path, O_RDONLY);
	int ret;

	if (fd < 0) {
		printf("head: %s: %s\n", path, strerror(fd));
		return 1;
	}
	ret = head_fd((int)fd, path, max_lines);
	close((int)fd);
	return ret;
}

int main(int argc, char **argv)
{
	long count = 10;
	int first = 1;
	int failed = 0;
	int files;

	while (first < argc && argv[first][0] == '-' &&
	       argv[first][1] != '\0') {
		const char *arg = argv[first];

		if (streq(arg, "--")) {
			first++;
			break;
		}
		if (streq(arg, "-n")) {
			if (first + 1 >= argc ||
			    parse_count(argv[first + 1], &count) < 0) {
				printf("usage: head [-n COUNT] [FILE...]\n");
				return 1;
			}
			first += 2;
			continue;
		}
		if (arg[1] >= '0' && arg[1] <= '9') {
			if (parse_count(arg + 1, &count) < 0) {
				printf("usage: head [-n COUNT] [FILE...]\n");
				return 1;
			}
			first++;
			continue;
		}
		printf("usage: head [-n COUNT] [FILE...]\n");
		return 1;
	}

	files = argc - first;
	if (files == 0)
		return head_fd(0, "-", count);

	for (int i = first; i < argc; i++) {
		if (files > 1)
			printf("%s==> %s <==\n", i == first ? "" : "\n",
			       argv[i]);
		if (streq(argv[i], "-")) {
			if (head_fd(0, "-", count) != 0)
				failed = 1;
		} else if (head_path(argv[i], count) != 0) {
			failed = 1;
		}
	}

	return failed;
}

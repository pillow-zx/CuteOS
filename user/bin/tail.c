#include <ulib.h>

#define TAIL_BUF_SIZE 4096

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

static int copy_from_line(int fd, const char *name, long start_line)
{
	char buf[256];
	long line = 0;
	int copying = start_line == 0;

	while (1) {
		long n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			printf("tail: %s: %s\n", name ? name : "-",
			       strerror(n));
			return 1;
		}
		if (n == 0)
			return 0;

		size_t off = 0;
		while (off < (size_t)n) {
			if (copying) {
				if (write(1, buf + off, (size_t)n - off) !=
				    n - (long)off)
					return 1;
				break;
			}
			if (buf[off++] == '\n') {
				line++;
				if (line >= start_line)
					copying = 1;
			}
		}
	}
}

static int tail_seekable(int fd, const char *name, long count)
{
	char buf[256];
	long lines = 0;
	int saw_data = 0;
	int last_nl = 0;
	long start_line;

	if (lseek(fd, 0, SEEK_SET) < 0)
		return -1;

	while (1) {
		long n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			printf("tail: %s: %s\n", name, strerror(n));
			return 1;
		}
		if (n == 0)
			break;
		saw_data = 1;
		last_nl = buf[n - 1] == '\n';
		for (long i = 0; i < n; i++) {
			if (buf[i] == '\n')
				lines++;
		}
	}
	if (saw_data && !last_nl)
		lines++;

	start_line = lines > count ? lines - count : 0;
	if (lseek(fd, 0, SEEK_SET) < 0)
		return -1;
	return copy_from_line(fd, name, start_line);
}

static int tail_stream(int fd, const char *name, long count)
{
	char ring[TAIL_BUF_SIZE];
	size_t head = 0;
	size_t used = 0;
	char buf[256];

	(void)count;
	while (1) {
		long n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			printf("tail: %s: %s\n", name ? name : "-",
			       strerror(n));
			return 1;
		}
		if (n == 0)
			break;
		for (long i = 0; i < n; i++) {
			ring[head] = buf[i];
			head = (head + 1) % sizeof(ring);
			if (used < sizeof(ring))
				used++;
		}
	}

	if (used == 0)
		return 0;

	size_t start = (head + sizeof(ring) - used) % sizeof(ring);
	long lines = 0;

	for (size_t i = 0; i < used; i++) {
		size_t pos = (head + sizeof(ring) - 1 - i) % sizeof(ring);

		if (ring[pos] == '\n') {
			lines++;
			if (lines > count) {
				start = (pos + 1) % sizeof(ring);
				used = i;
				break;
			}
		}
	}

	for (size_t i = 0; i < used; i++) {
		char c = ring[(start + i) % sizeof(ring)];

		if (write(1, &c, 1) != 1)
			return 1;
	}
	return 0;
}

static int tail_fd(int fd, const char *name, long count)
{
	int ret = tail_seekable(fd, name, count);

	if (ret >= 0)
		return ret;
	return tail_stream(fd, name, count);
}

static int tail_path(const char *path, long count)
{
	long fd = open(path, O_RDONLY);
	int ret;

	if (fd < 0) {
		printf("tail: %s: %s\n", path, strerror(fd));
		return 1;
	}
	ret = tail_fd((int)fd, path, count);
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
				printf("usage: tail [-n COUNT] [FILE...]\n");
				return 1;
			}
			first += 2;
			continue;
		}
		if (arg[1] >= '0' && arg[1] <= '9') {
			if (parse_count(arg + 1, &count) < 0) {
				printf("usage: tail [-n COUNT] [FILE...]\n");
				return 1;
			}
			first++;
			continue;
		}
		printf("usage: tail [-n COUNT] [FILE...]\n");
		return 1;
	}

	files = argc - first;
	if (files == 0)
		return tail_fd(0, "-", count);

	for (int i = first; i < argc; i++) {
		if (files > 1)
			printf("%s==> %s <==\n", i == first ? "" : "\n",
			       argv[i]);
		if (streq(argv[i], "-")) {
			if (tail_fd(0, "-", count) != 0)
				failed = 1;
		} else if (tail_path(argv[i], count) != 0) {
			failed = 1;
		}
	}

	return failed;
}

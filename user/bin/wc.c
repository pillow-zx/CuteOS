#include <ulib.h>

struct wc_count {
	unsigned long lines;
	unsigned long words;
	unsigned long bytes;
};

static int is_space(char c)
{
	return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f' ||
	       c == '\v';
}

static int count_fd(int fd, const char *name, struct wc_count *count)
{
	char buf[256];
	int in_word = 0;

	memset(count, 0, sizeof(*count));
	while (1) {
		long n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			printf("wc: %s: %s\n", name ? name : "-",
			       strerror(n));
			return 1;
		}
		if (n == 0)
			break;
		count->bytes += (unsigned long)n;
		for (long i = 0; i < n; i++) {
			if (buf[i] == '\n')
				count->lines++;
			if (is_space(buf[i])) {
				in_word = 0;
			} else if (!in_word) {
				count->words++;
				in_word = 1;
			}
		}
	}
	return 0;
}

static int count_path(const char *path, struct wc_count *count)
{
	long fd = open(path, O_RDONLY);
	int ret;

	if (fd < 0) {
		printf("wc: %s: %s\n", path, strerror(fd));
		return 1;
	}
	ret = count_fd((int)fd, path, count);
	close((int)fd);
	return ret;
}

static void print_count(const struct wc_count *count, const char *name)
{
	printf("%lu %lu %lu", count->lines, count->words, count->bytes);
	if (name)
		printf(" %s", name);
	printf("\n");
}

int main(int argc, char **argv)
{
	struct wc_count count;
	struct wc_count total = {0, 0, 0};
	int failed = 0;

	if (argc == 1) {
		if (count_fd(0, "-", &count) != 0)
			return 1;
		print_count(&count, NULL);
		return 0;
	}

	for (int i = 1; i < argc; i++) {
		int ret;

		if (streq(argv[i], "-"))
			ret = count_fd(0, "-", &count);
		else
			ret = count_path(argv[i], &count);
		if (ret != 0) {
			failed = 1;
			continue;
		}
		print_count(&count, argv[i]);
		total.lines += count.lines;
		total.words += count.words;
		total.bytes += count.bytes;
	}
	if (argc > 2)
		print_count(&total, "total");
	return failed;
}

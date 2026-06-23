#include <ulib.h>

struct grep_options {
	int line_number;
	int count_only;
	int quiet;
	int invert;
};

static int contains_fixed(const char *line, size_t len, const char *pat,
			  size_t pat_len)
{
	if (pat_len == 0)
		return 1;
	if (pat_len > len)
		return 0;
	for (size_t i = 0; i + pat_len <= len; i++) {
		if (line[i] == pat[0] && strncmp(line + i, pat, pat_len) == 0)
			return 1;
	}
	return 0;
}

static int grep_fd(int fd, const char *name, const char *pat,
		   const struct grep_options *opts, int show_name)
{
	char line[1024];
	size_t len = 0;
	unsigned long line_no = 1;
	unsigned long matches = 0;
	size_t pat_len = strlen(pat);

	while (1) {
		char c;
		long n = read(fd, &c, 1);

		if (n < 0) {
			printf("grep: %s: %s\n", name ? name : "-",
			       strerror(n));
			return 2;
		}
		if (n == 0 && len == 0)
			break;
		if (n > 0 && len + 1 < sizeof(line))
			line[len++] = c;
		if (n > 0 && c != '\n')
			continue;

		int matched = contains_fixed(line, len, pat, pat_len);

		if (opts->invert)
			matched = !matched;
		if (matched) {
			matches++;
			if (opts->quiet)
				return 0;
			if (!opts->count_only) {
				if (show_name)
					printf("%s:", name);
				if (opts->line_number)
					printf("%lu:", line_no);
				write(1, line, len);
				if (len == 0 || line[len - 1] != '\n')
					printf("\n");
			}
		}
		len = 0;
		line_no++;
		if (n == 0)
			break;
	}

	if (opts->count_only) {
		if (show_name)
			printf("%s:", name);
		printf("%lu\n", matches);
	}
	return matches > 0 ? 0 : 1;
}

static int grep_path(const char *path, const char *pat,
		     const struct grep_options *opts, int show_name)
{
	long fd = open(path, O_RDONLY);
	int ret;

	if (fd < 0) {
		if (!opts->quiet)
			printf("grep: %s: %s\n", path, strerror(fd));
		return 2;
	}
	ret = grep_fd((int)fd, path, pat, opts, show_name);
	close((int)fd);
	return ret;
}

int main(int argc, char **argv)
{
	struct grep_options opts = {0, 0, 0, 0};
	int first = 1;
	const char *pattern;
	int status = 1;
	int files;

	while (first < argc && argv[first][0] == '-' &&
	       argv[first][1] != '\0') {
		const char *arg = argv[first];

		if (streq(arg, "--")) {
			first++;
			break;
		}
		for (int j = 1; arg[j] != '\0'; j++) {
			if (arg[j] == 'F') {
				/* Fixed string matching is the only mode. */
			} else if (arg[j] == 'n') {
				opts.line_number = 1;
			} else if (arg[j] == 'c') {
				opts.count_only = 1;
			} else if (arg[j] == 'q') {
				opts.quiet = 1;
			} else if (arg[j] == 'v') {
				opts.invert = 1;
			} else {
				printf("usage: grep [-F] [-ncvq] PATTERN [FILE...]\n");
				return 2;
			}
		}
		first++;
	}

	if (first >= argc) {
		printf("usage: grep [-F] [-ncvq] PATTERN [FILE...]\n");
		return 2;
	}
	pattern = argv[first++];
	files = argc - first;
	if (files == 0)
		return grep_fd(0, "-", pattern, &opts, 0);

	for (int i = first; i < argc; i++) {
		int ret;

		if (streq(argv[i], "-"))
			ret = grep_fd(0, "-", pattern, &opts, files > 1);
		else
			ret = grep_path(argv[i], pattern, &opts, files > 1);
		if (ret == 0)
			status = 0;
		else if (ret == 2)
			status = 2;
	}

	return status;
}

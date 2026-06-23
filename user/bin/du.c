#include <ulib.h>

struct du_options {
	int summarize;
	int all;
};

static unsigned long blocks_to_k(unsigned long blocks)
{
	return (blocks + 1) / 2;
}

static int du_path(const char *path, const struct du_options *opts,
		   unsigned long *total);

static int du_dir(const char *path, const struct du_options *opts,
		  unsigned long *total)
{
	char buf[512];
	char child[PATH_MAX];
	int failed = 0;
	long fd = open(path, O_RDONLY | O_DIRECTORY);

	if (fd < 0) {
		printf("du: %s: %s\n", path, strerror(fd));
		return 1;
	}

	while (1) {
		long n = getdents64((int)fd, buf, sizeof(buf));
		long off = 0;

		if (n < 0) {
			printf("du: %s: %s\n", path, strerror(n));
			failed = 1;
			break;
		}
		if (n == 0)
			break;

		while (off < n) {
			struct linux_dirent64 *de =
				(struct linux_dirent64 *)(buf + off);
			unsigned long child_total = 0;

			if (de->d_name[0] != '\0' &&
			    !is_dot_or_dotdot(de->d_name)) {
				if (path_join(child, sizeof(child), path,
					      de->d_name) < 0) {
					printf("du: %s/%s: path too long\n",
					       path, de->d_name);
					failed = 1;
				} else if (du_path(child, opts, &child_total) !=
					   0) {
					failed = 1;
				}
				*total += child_total;
			}
			off += de->d_reclen;
		}
	}

	close((int)fd);
	return failed;
}

static int du_path(const char *path, const struct du_options *opts,
		   unsigned long *total)
{
	struct stat st;
	unsigned long self;
	int failed = 0;
	long ret = fstatat(AT_FDCWD, path, &st, AT_SYMLINK_NOFOLLOW);

	*total = 0;
	if (ret < 0) {
		printf("du: %s: %s\n", path, strerror(ret));
		return 1;
	}

	self = blocks_to_k((unsigned long)st.st_blocks);
	*total = self;
	if (S_ISDIR(st.st_mode)) {
		failed = du_dir(path, opts, total);
		if (!opts->summarize)
			printf("%lu %s\n", *total, path);
	} else if (opts->all && !opts->summarize) {
		printf("%lu %s\n", self, path);
	}

	return failed;
}

int main(int argc, char **argv)
{
	struct du_options opts = {0, 0};
	int first = 1;
	int failed = 0;

	while (first < argc && argv[first][0] == '-' &&
	       argv[first][1] != '\0') {
		const char *arg = argv[first];

		if (streq(arg, "--")) {
			first++;
			break;
		}
		for (int j = 1; arg[j] != '\0'; j++) {
			if (arg[j] == 's')
				opts.summarize = 1;
			else if (arg[j] == 'a')
				opts.all = 1;
			else {
				printf("usage: du [-a] [-s] [FILE...]\n");
				return 1;
			}
		}
		first++;
	}

	if (first >= argc) {
		unsigned long total = 0;

		if (du_path(".", &opts, &total) != 0)
			return 1;
		if (opts.summarize)
			printf("%lu .\n", total);
		return 0;
	}

	for (int i = first; i < argc; i++) {
		unsigned long total = 0;

		if (du_path(argv[i], &opts, &total) != 0)
			failed = 1;
		if (opts.summarize)
			printf("%lu %s\n", total, argv[i]);
	}
	return failed;
}

#include <ulib.h>

#define FIND_MAX_PATHS 16

struct find_options {
	const char *name;
	int type;
	long maxdepth;
};

static const char *base_name(const char *path)
{
	const char *slash = strrchr(path, '/');

	if (!slash)
		return path;
	if (slash[1] == '\0')
		return path;
	return slash + 1;
}

static int match_pattern(const char *s, const char *p)
{
	if (!p)
		return 1;
	if (*p == '\0')
		return *s == '\0';
	if (*p == '*') {
		while (p[1] == '*')
			p++;
		if (match_pattern(s, p + 1))
			return 1;
		return *s && match_pattern(s + 1, p);
	}
	if (*p == '?')
		return *s && match_pattern(s + 1, p + 1);
	return *s == *p && match_pattern(s + 1, p + 1);
}

static int mode_type(unsigned int mode)
{
	if (S_ISREG(mode))
		return 'f';
	if (S_ISDIR(mode))
		return 'd';
	if (S_ISLNK(mode))
		return 'l';
	if (S_ISCHR(mode))
		return 'c';
	if (S_ISBLK(mode))
		return 'b';
	if (S_ISFIFO(mode))
		return 'p';
	return 0;
}

static int should_print(const char *path, const struct stat *st,
			const struct find_options *opts)
{
	if (opts->type && mode_type(st->st_mode) != opts->type)
		return 0;
	if (opts->name && !match_pattern(base_name(path), opts->name))
		return 0;
	return 1;
}

static int find_path(const char *path, const struct find_options *opts,
		     long depth)
{
	struct stat st;
	char buf[512];
	char child[PATH_MAX];
	int failed = 0;
	long ret = fstatat(AT_FDCWD, path, &st, AT_SYMLINK_NOFOLLOW);

	if (ret < 0) {
		printf("find: %s: %s\n", path, strerror(ret));
		return 1;
	}

	if (should_print(path, &st, opts))
		printf("%s\n", path);

	if (!S_ISDIR(st.st_mode))
		return 0;
	if (opts->maxdepth >= 0 && depth >= opts->maxdepth)
		return 0;

	long fd = open(path, O_RDONLY | O_DIRECTORY);

	if (fd < 0) {
		printf("find: %s: %s\n", path, strerror(fd));
		return 1;
	}

	while (1) {
		long n = getdents64((int)fd, buf, sizeof(buf));
		long off = 0;

		if (n < 0) {
			printf("find: %s: %s\n", path, strerror(n));
			failed = 1;
			break;
		}
		if (n == 0)
			break;

		while (off < n) {
			struct linux_dirent64 *de =
				(struct linux_dirent64 *)(buf + off);

			if (de->d_name[0] != '\0' &&
			    !is_dot_or_dotdot(de->d_name)) {
				if (path_join(child, sizeof(child), path,
					      de->d_name) < 0) {
					printf("find: %s/%s: path too long\n",
					       path, de->d_name);
					failed = 1;
				} else if (find_path(child, opts, depth + 1) !=
					   0) {
					failed = 1;
				}
			}
			off += de->d_reclen;
		}
	}

	close((int)fd);
	return failed;
}

static int parse_type(const char *arg)
{
	if (!arg || arg[0] == '\0' || arg[1] != '\0')
		return 0;
	if (arg[0] == 'f' || arg[0] == 'd' || arg[0] == 'l' || arg[0] == 'c' ||
	    arg[0] == 'b' || arg[0] == 'p')
		return arg[0];
	return 0;
}

int main(int argc, char **argv)
{
	const char *paths[FIND_MAX_PATHS];
	int npaths = 0;
	int i = 1;
	struct find_options opts = {NULL, 0, -1};
	int failed = 0;

	while (i < argc && argv[i][0] != '-') {
		if (npaths >= FIND_MAX_PATHS) {
			printf("find: too many paths\n");
			return 1;
		}
		paths[npaths++] = argv[i++];
	}
	if (npaths == 0)
		paths[npaths++] = ".";

	while (i < argc) {
		if (streq(argv[i], "-name")) {
			if (i + 1 >= argc)
				goto usage;
			opts.name = argv[i + 1];
			i += 2;
		} else if (streq(argv[i], "-type")) {
			if (i + 1 >= argc)
				goto usage;
			opts.type = parse_type(argv[i + 1]);
			if (!opts.type)
				goto usage;
			i += 2;
		} else if (streq(argv[i], "-maxdepth")) {
			if (i + 1 >= argc)
				goto usage;
			opts.maxdepth = atoi(argv[i + 1]);
			if (opts.maxdepth < 0)
				goto usage;
			i += 2;
		} else {
			goto usage;
		}
	}

	for (int p = 0; p < npaths; p++) {
		if (find_path(paths[p], &opts, 0) != 0)
			failed = 1;
	}
	return failed;

usage:
	printf("usage: find [PATH...] [-name PATTERN] ");
	printf("[-type f|d|l|c|b|p] [-maxdepth N]\n");
	return 1;
}

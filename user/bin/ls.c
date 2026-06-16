#include <ulib.h>

struct ls_options {
	int long_format;
};

static char file_type_char(unsigned int mode)
{
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
	return '-';
}

static void format_mode(unsigned int mode, char buf[11])
{
	buf[0] = file_type_char(mode);
	buf[1] = (mode & S_IRUSR) ? 'r' : '-';
	buf[2] = (mode & S_IWUSR) ? 'w' : '-';
	buf[3] = (mode & S_IXUSR) ? 'x' : '-';
	buf[4] = (mode & S_IRGRP) ? 'r' : '-';
	buf[5] = (mode & S_IWGRP) ? 'w' : '-';
	buf[6] = (mode & S_IXGRP) ? 'x' : '-';
	buf[7] = (mode & S_IROTH) ? 'r' : '-';
	buf[8] = (mode & S_IWOTH) ? 'w' : '-';
	buf[9] = (mode & S_IXOTH) ? 'x' : '-';
	buf[10] = '\0';
}

static void print_name_suffix(const char *name, unsigned int mode)
{
	printf("%s", name);
	if (S_ISDIR(mode))
		printf("/");
	else if (S_ISLNK(mode))
		printf("@");
	printf("\n");
}

static void print_long_entry(const char *name, const struct stat *st)
{
	char mode[11];

	format_mode(st->st_mode, mode);
	printf("%s %lu %lu %lu %ld %s", mode, (unsigned long)st->st_nlink,
	       (unsigned long)st->st_uid, (unsigned long)st->st_gid,
	       st->st_size, name);
	if (S_ISDIR(st->st_mode))
		printf("/");
	else if (S_ISLNK(st->st_mode))
		printf("@");
	printf("\n");
}

static int list_one(const char *path, const char *name,
		    const struct ls_options *opts)
{
	struct stat st;
	long ret;

	ret = fstatat(AT_FDCWD, path, &st, AT_SYMLINK_NOFOLLOW);
	if (ret < 0) {
		printf("ls: %s: %s\n", path, strerror(ret));
		return 1;
	}

	if (opts->long_format)
		print_long_entry(name, &st);
	else
		print_name_suffix(name, st.st_mode);

	return 0;
}

static int list_dir(const char *path, const struct ls_options *opts)
{
	char buf[512];
	char full[PATH_MAX];
	int failed = 0;
	long fd = open(path, O_RDONLY | O_DIRECTORY);

	if (fd < 0) {
		printf("ls: %s: %s\n", path, strerror(fd));
		return 1;
	}

	while (1) {
		long n = getdents64((int)fd, buf, sizeof(buf));
		long off = 0;

		if (n < 0) {
			printf("ls: %s: %s\n", path, strerror(n));
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
				if (path_join(full, sizeof(full), path,
					      de->d_name) < 0) {
					printf("ls: %s/%s: path too long\n",
					       path, de->d_name);
					failed = 1;
				} else if (list_one(full, de->d_name, opts) !=
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

static int list_path(const char *path, const struct ls_options *opts)
{
	struct stat st;
	long ret = fstatat(AT_FDCWD, path, &st, AT_SYMLINK_NOFOLLOW);

	if (ret < 0) {
		printf("ls: %s: %s\n", path, strerror(ret));
		return 1;
	}
	if (S_ISDIR(st.st_mode))
		return list_dir(path, opts);
	if (opts->long_format)
		print_long_entry(path, &st);
	else
		print_name_suffix(path, st.st_mode);
	return 0;
}

static int parse_options(int argc, char **argv, struct ls_options *opts,
			 int *first_path)
{
	int i = 1;

	memset(opts, 0, sizeof(*opts));

	while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
		const char *arg = argv[i];

		if (streq(arg, "--")) {
			i++;
			break;
		}
		for (int j = 1; arg[j] != '\0'; j++) {
			if (arg[j] == 'l')
				opts->long_format = 1;
			else {
				printf("usage: ls [-l] [FILE...]\n");
				return -1;
			}
		}
		i++;
	}

	*first_path = i;
	return 0;
}

int main(int argc, char **argv)
{
	struct ls_options opts;
	int failed = 0;
	int first_path;

	if (parse_options(argc, argv, &opts, &first_path) < 0)
		return 1;

	if (first_path >= argc)
		return list_path(".", &opts);

	for (int i = first_path; i < argc; i++) {
		if (argc - first_path > 1)
			printf("%s:\n", argv[i]);
		if (list_path(argv[i], &opts) != 0)
			failed = 1;
		if (i + 1 < argc)
			printf("\n");
	}
	return failed;
}

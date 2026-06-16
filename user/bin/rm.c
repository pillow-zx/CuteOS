#include <ulib.h>

struct rm_options {
	int force;
	int recursive;
};

static int rm_path(const char *path, const struct rm_options *opts);

static int rm_dir_recursive(const char *path, const struct rm_options *opts)
{
	char buf[512];
	char child[PATH_MAX];
	int failed = 0;
	long fd = open(path, O_RDONLY | O_DIRECTORY);

	if (fd < 0) {
		if (!opts->force || fd != -ENOENT)
			printf("rm: %s: %s\n", path, strerror(fd));
		return opts->force && fd == -ENOENT ? 0 : 1;
	}

	while (1) {
		long n = getdents64((int)fd, buf, sizeof(buf));
		long off = 0;

		if (n < 0) {
			printf("rm: %s: %s\n", path, strerror(n));
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
					printf("rm: %s/%s: path too long\n",
					       path, de->d_name);
					failed = 1;
				} else if (rm_path(child, opts) != 0) {
					failed = 1;
				}
			}
			off += de->d_reclen;
		}
	}

	close((int)fd);

	long ret = unlinkat(AT_FDCWD, path, AT_REMOVEDIR);

	if (ret < 0) {
		if (!opts->force || ret != -ENOENT)
			printf("rm: %s: %s\n", path, strerror(ret));
		return opts->force && ret == -ENOENT ? failed : 1;
	}

	return failed;
}

static int rm_path(const char *path, const struct rm_options *opts)
{
	struct stat st;
	long ret = fstatat(AT_FDCWD, path, &st, AT_SYMLINK_NOFOLLOW);

	if (ret < 0) {
		if (!opts->force || ret != -ENOENT)
			printf("rm: %s: %s\n", path, strerror(ret));
		return opts->force && ret == -ENOENT ? 0 : 1;
	}

	if (S_ISDIR(st.st_mode)) {
		if (!opts->recursive) {
			printf("rm: %s: is a directory\n", path);
			return 1;
		}
		return rm_dir_recursive(path, opts);
	}

	ret = unlinkat(AT_FDCWD, path, 0);
	if (ret < 0) {
		if (!opts->force || ret != -ENOENT)
			printf("rm: %s: %s\n", path, strerror(ret));
		return opts->force && ret == -ENOENT ? 0 : 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct rm_options opts = {0, 0};
	int failed = 0;
	int first_path = 1;

	while (first_path < argc && argv[first_path][0] == '-' &&
	       argv[first_path][1] != '\0') {
		const char *arg = argv[first_path];

		if (streq(arg, "--")) {
			first_path++;
			break;
		}
		for (int j = 1; arg[j] != '\0'; j++) {
			if (arg[j] == 'f')
				opts.force = 1;
			else if (arg[j] == 'r' || arg[j] == 'R')
				opts.recursive = 1;
			else {
				printf("usage: rm [-f] [-r] FILE...\n");
				return 1;
			}
		}
		first_path++;
	}

	if (first_path >= argc) {
		printf("usage: rm [-f] [-r] FILE...\n");
		return 1;
	}

	for (int i = first_path; i < argc; i++) {
		if (rm_path(argv[i], &opts) != 0)
			failed = 1;
	}
	return failed;
}

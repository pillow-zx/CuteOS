#include <ulib.h>

#define PATH_MAX 512

struct cp_options {
	int recursive;
};

static const char *base_name(const char *path)
{
	const char *end = path + strlen(path);

	while (end > path + 1 && end[-1] == '/')
		end--;
	while (end > path && end[-1] != '/')
		end--;
	return end;
}

static int path_join(char *buf, size_t size, const char *dir, const char *name)
{
	int len;

	if (streq(dir, "/"))
		len = snprintf(buf, size, "/%s", name);
	else
		len = snprintf(buf, size, "%s/%s", dir, name);

	return len >= 0 && (size_t)len < size ? 0 : -1;
}

static int copy_file_to(const char *src, const char *dst)
{
	char buf[256];
	int failed = 0;
	long in = open(src, O_RDONLY);
	long out;

	if (in < 0) {
		printf("cp: %s: error %ld\n", src, in);
		return 1;
	}

	out = openat(AT_FDCWD, dst, O_CREAT | O_TRUNC | O_WRONLY, 0666);
	if (out < 0) {
		printf("cp: %s: error %ld\n", dst, out);
		close((int)in);
		return 1;
	}

	while (1) {
		long n = read((int)in, buf, sizeof(buf));

		if (n < 0) {
			printf("cp: %s: error %ld\n", src, n);
			failed = 1;
			break;
		}
		if (n == 0)
			break;
		if (write((int)out, buf, (size_t)n) != n) {
			printf("cp: %s: write error\n", dst);
			failed = 1;
			break;
		}
	}

	close((int)in);
	close((int)out);
	return failed;
}

static int copy_exact(const char *src, const char *dst,
		      const struct cp_options *opts);

static int copy_dir_contents(const char *src, const char *dst,
			     const struct cp_options *opts)
{
	char buf[512];
	char src_child[PATH_MAX];
	char dst_child[PATH_MAX];
	int failed = 0;
	long fd = open(src, O_RDONLY | O_DIRECTORY);

	if (fd < 0) {
		printf("cp: %s: error %ld\n", src, fd);
		return 1;
	}

	while (1) {
		long n = getdents64((int)fd, buf, sizeof(buf));
		long off = 0;

		if (n < 0) {
			printf("cp: %s: error %ld\n", src, n);
			failed = 1;
			break;
		}
		if (n == 0)
			break;

		while (off < n) {
			struct linux_dirent64 *de =
				(struct linux_dirent64 *)(buf + off);

			if (de->d_name[0] != '\0' &&
			    !streq(de->d_name, ".") &&
			    !streq(de->d_name, "..")) {
				if (path_join(src_child, sizeof(src_child), src,
					      de->d_name) < 0 ||
				    path_join(dst_child, sizeof(dst_child), dst,
					      de->d_name) < 0) {
					printf("cp: %s/%s: path too long\n", src,
					       de->d_name);
					failed = 1;
				} else if (copy_exact(src_child, dst_child,
						      opts) != 0) {
					failed = 1;
				}
			}
			off += de->d_reclen;
		}
	}

	close((int)fd);
	return failed;
}

static int copy_dir_to(const char *src, const char *dst,
		       const struct cp_options *opts)
{
	struct stat st;
	long ret = fstatat(AT_FDCWD, dst, &st, AT_SYMLINK_NOFOLLOW);

	if (ret < 0) {
		if (ret != -ENOENT) {
			printf("cp: %s: error %ld\n", dst, ret);
			return 1;
		}
		ret = mkdirat(AT_FDCWD, dst, 0777);
		if (ret < 0) {
			printf("cp: %s: error %ld\n", dst, ret);
			return 1;
		}
	} else if (!S_ISDIR(st.st_mode)) {
		printf("cp: %s: not a directory\n", dst);
		return 1;
	}

	return copy_dir_contents(src, dst, opts);
}

static int copy_exact(const char *src, const char *dst,
		      const struct cp_options *opts)
{
	struct stat src_st;
	long ret = fstatat(AT_FDCWD, src, &src_st, AT_SYMLINK_NOFOLLOW);

	if (ret < 0) {
		printf("cp: %s: error %ld\n", src, ret);
		return 1;
	}

	if (S_ISDIR(src_st.st_mode)) {
		if (!opts->recursive) {
			printf("cp: %s: is a directory\n", src);
			return 1;
		}
		return copy_dir_to(src, dst, opts);
	}

	return copy_file_to(src, dst);
}

static int copy_path(const char *src, const char *dst,
		     const struct cp_options *opts)
{
	struct stat dst_st;
	char target[PATH_MAX];
	struct stat src_st;
	long ret;

	ret = fstatat(AT_FDCWD, src, &src_st, AT_SYMLINK_NOFOLLOW);
	if (ret < 0) {
		printf("cp: %s: error %ld\n", src, ret);
		return 1;
	}

	ret = fstatat(AT_FDCWD, dst, &dst_st, AT_SYMLINK_NOFOLLOW);
	if (ret == 0 && S_ISDIR(dst_st.st_mode)) {
		if (path_join(target, sizeof(target), dst, base_name(src)) < 0) {
			printf("cp: %s/%s: path too long\n", dst, base_name(src));
			return 1;
		}
		dst = target;
	} else if (ret < 0 && ret != -ENOENT) {
		printf("cp: %s: error %ld\n", dst, ret);
		return 1;
	}

	return copy_exact(src, dst, opts);
}

int main(int argc, char **argv)
{
	struct cp_options opts = { 0 };
	int first_arg = 1;

	while (first_arg < argc && argv[first_arg][0] == '-' &&
	       argv[first_arg][1] != '\0') {
		const char *arg = argv[first_arg];

		if (streq(arg, "--")) {
			first_arg++;
			break;
		}
		for (int j = 1; arg[j] != '\0'; j++) {
			if (arg[j] == 'r' || arg[j] == 'R')
				opts.recursive = 1;
			else {
				printf("usage: cp [-r] SRC DST\n");
				return 1;
			}
		}
		first_arg++;
	}

	if (argc - first_arg != 2) {
		printf("usage: cp [-r] SRC DST\n");
		return 1;
	}

	return copy_path(argv[first_arg], argv[first_arg + 1], &opts);
}

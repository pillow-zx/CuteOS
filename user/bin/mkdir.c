#include <ulib.h>

static int ensure_directory(const char *path, int mode, int quiet_exists)
{
	struct stat st;
	long ret = fstatat(AT_FDCWD, path, &st, AT_SYMLINK_NOFOLLOW);

	if (ret == 0) {
		if (S_ISDIR(st.st_mode))
			return 0;
		printf("mkdir: %s: exists and is not a directory\n", path);
		return 1;
	}
	if (ret != -ENOENT) {
		printf("mkdir: %s: %s\n", path, strerror(ret));
		return 1;
	}

	ret = mkdirat(AT_FDCWD, path, mode);
	if (ret < 0) {
		if (!quiet_exists || ret != -EEXIST)
			printf("mkdir: %s: %s\n", path, strerror(ret));
		return ret == -EEXIST ? 0 : 1;
	}
	return 0;
}

static int mkdir_parents(const char *path, int mode)
{
	char buf[PATH_MAX];
	size_t len = strlen(path);

	if (len == 0 || len >= sizeof(buf)) {
		printf("mkdir: %s: path too long\n", path);
		return 1;
	}

	strcpy(buf, path);
	while (len > 1 && buf[len - 1] == '/') {
		buf[len - 1] = '\0';
		len--;
	}
	if (streq(buf, "/"))
		return 0;

	for (size_t i = 1; buf[i] != '\0'; i++) {
		if (buf[i] != '/')
			continue;
		buf[i] = '\0';
		if (ensure_directory(buf, mode, 1) != 0)
			return 1;
		buf[i] = '/';
	}

	return ensure_directory(buf, mode, 1);
}

int main(int argc, char **argv)
{
	int failed = 0;
	int parents = 0;
	int first_path = 1;

	while (first_path < argc && argv[first_path][0] == '-' &&
	       argv[first_path][1] != '\0') {
		const char *arg = argv[first_path];

		if (streq(arg, "--")) {
			first_path++;
			break;
		}
		for (int j = 1; arg[j] != '\0'; j++) {
			if (arg[j] == 'p')
				parents = 1;
			else {
				printf("usage: mkdir [-p] DIR...\n");
				return 1;
			}
		}
		first_path++;
	}

	if (first_path >= argc) {
		printf("usage: mkdir [-p] DIR...\n");
		return 1;
	}

	for (int i = first_path; i < argc; i++) {
		int ret;

		if (parents)
			ret = mkdir_parents(argv[i], 0777);
		else
			ret = ensure_directory(argv[i], 0777, 0);
		if (ret != 0)
			failed = 1;
	}

	return failed;
}

#include <ulib.h>

static void print_name(const char *name, unsigned char type)
{
	print(name);
	if (type == DT_DIR)
		print("/");
	else if (type == DT_LNK)
		print("@");
	print("\n");
}

static int list_dir(const char *path)
{
	char buf[512];
	long fd = open(path, O_RDONLY);

	if (fd < 0) {
		print_error("ls", path, fd);
		return 1;
	}

	while (1) {
		long n = getdents64((int)fd, buf, sizeof(buf));
		long off = 0;

		if (n < 0) {
			print_error("ls", path, n);
			close((int)fd);
			return 1;
		}
		if (n == 0)
			break;

		while (off < n) {
			struct linux_dirent64 *de =
				(struct linux_dirent64 *)(buf + off);

			if (de->d_name[0] != '\0')
				print_name(de->d_name, de->d_type);
			off += de->d_reclen;
		}
	}

	close((int)fd);
	return 0;
}

static int list_path(const char *path)
{
	struct stat st;
	long ret = fstatat(AT_FDCWD, path, &st, 0);

	if (ret < 0) {
		print_error("ls", path, ret);
		return 1;
	}
	if (!S_ISDIR(st.st_mode)) {
		print(path);
		print("\n");
		return 0;
	}
	return list_dir(path);
}

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc == 1)
		return list_path(".");

	for (int i = 1; i < argc; i++) {
		if (argc > 2) {
			print(argv[i]);
			print(":\n");
		}
		if (list_path(argv[i]) != 0)
			failed = 1;
	}
	return failed;
}

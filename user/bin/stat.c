#include <ulib.h>

static const char *type_name(unsigned int mode)
{
	if (S_ISDIR(mode))
		return "dir";
	if (S_ISREG(mode))
		return "file";
	if (S_ISLNK(mode))
		return "link";
	if (S_ISCHR(mode))
		return "char";
	if (S_ISBLK(mode))
		return "block";
	if (S_ISFIFO(mode))
		return "fifo";
	return "unknown";
}

static int stat_one(const char *path)
{
	struct stat st;
	long ret = fstatat(AT_FDCWD, path, &st, AT_SYMLINK_NOFOLLOW);

	if (ret < 0) {
		print_error("stat", path, ret);
		return 1;
	}

	print(path);
	print(": type=");
	print(type_name(st.st_mode));
	print(" size=");
	print_dec((unsigned long)st.st_size);
	print(" mode=");
	print_hex(st.st_mode);
	print(" uid=");
	print_dec(st.st_uid);
	print(" gid=");
	print_dec(st.st_gid);
	print(" links=");
	print_dec(st.st_nlink);
	print("\n");
	return 0;
}

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc < 2) {
		print("usage: stat FILE...\n");
		return 1;
	}

	for (int i = 1; i < argc; i++) {
		if (stat_one(argv[i]) != 0)
			failed = 1;
	}
	return failed;
}

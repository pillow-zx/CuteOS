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
		printf("stat: %s: %s\n", path, strerror(ret));
		return 1;
	}

	printf("%s: type=%s size=%lu mode=0x%lx uid=%lu gid=%lu links=%lu\n",
	       path, type_name(st.st_mode), (unsigned long)st.st_size,
	       (unsigned long)st.st_mode, (unsigned long)st.st_uid,
	       (unsigned long)st.st_gid, (unsigned long)st.st_nlink);
	return 0;
}

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc < 2) {
		printf("usage: stat FILE...\n");
		return 1;
	}

	for (int i = 1; i < argc; i++) {
		if (stat_one(argv[i]) != 0)
			failed = 1;
	}
	return failed;
}

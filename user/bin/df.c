#include <ulib.h>

static unsigned long blocks_to_k(unsigned long blocks, unsigned long bsize)
{
	return (blocks * bsize + 1023) / 1024;
}

static int df_one(const char *path)
{
	struct statfs64 st;
	unsigned long size;
	unsigned long used;
	unsigned long avail;
	long ret = statfs64(path, &st);

	if (ret < 0) {
		printf("df: %s: %s\n", path, strerror(ret));
		return 1;
	}

	size = blocks_to_k(st.f_blocks, st.f_bsize);
	avail = blocks_to_k(st.f_bavail, st.f_bsize);
	used = size > avail ? size - avail : 0;

	printf("ext2 %lu %lu %lu %s\n", size, used, avail, path);
	return 0;
}

int main(int argc, char **argv)
{
	int failed = 0;

	printf("Filesystem 1K-blocks Used Available Mounted-on\n");
	if (argc == 1)
		return df_one("/");

	for (int i = 1; i < argc; i++) {
		if (df_one(argv[i]) != 0)
			failed = 1;
	}
	return failed;
}

#include <ulib.h>

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc < 2) {
		printf("usage: rm FILE...\n");
		return 1;
	}

	for (int i = 1; i < argc; i++) {
		long ret = unlinkat(AT_FDCWD, argv[i], 0);

		if (ret < 0) {
			printf("rm: %s: error %ld\n", argv[i], ret);
			failed = 1;
		}
	}
	return failed;
}

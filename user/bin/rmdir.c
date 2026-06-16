#include <ulib.h>

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc < 2) {
		print("usage: rmdir DIR...\n");
		return 1;
	}

	for (int i = 1; i < argc; i++) {
		long ret = unlinkat(AT_FDCWD, argv[i], AT_REMOVEDIR);

		if (ret < 0) {
			print_error("rmdir", argv[i], ret);
			failed = 1;
		}
	}
	return failed;
}

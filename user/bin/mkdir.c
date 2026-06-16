#include <ulib.h>

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc < 2) {
		print("usage: mkdir DIR...\n");
		return 1;
	}

	for (int i = 1; i < argc; i++) {
		long ret = mkdirat(AT_FDCWD, argv[i], 0777);

		if (ret < 0) {
			print_error("mkdir", argv[i], ret);
			failed = 1;
		}
	}
	return failed;
}

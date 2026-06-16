#include <ulib.h>

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc < 2) {
		print("usage: touch FILE...\n");
		return 1;
	}

	for (int i = 1; i < argc; i++) {
		long fd = openat(AT_FDCWD, argv[i],
				 O_CREAT | O_WRONLY, 0666);

		if (fd < 0) {
			print_error("touch", argv[i], fd);
			failed = 1;
			continue;
		}
		close((int)fd);
	}
	return failed;
}

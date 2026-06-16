#include <ulib.h>

int main(int argc, char **argv)
{
	char buf[256];
	long in;
	long out;
	int failed = 0;

	if (argc != 3) {
		printf("usage: cp SRC DST\n");
		return 1;
	}

	in = open(argv[1], O_RDONLY);
	if (in < 0) {
		printf("cp: %s: error %ld\n", argv[1], in);
		return 1;
	}

	out = openat(AT_FDCWD, argv[2], O_CREAT | O_TRUNC | O_WRONLY, 0666);
	if (out < 0) {
		printf("cp: %s: error %ld\n", argv[2], out);
		close((int)in);
		return 1;
	}

	while (1) {
		long n = read((int)in, buf, sizeof(buf));

		if (n < 0) {
			printf("cp: %s: error %ld\n", argv[1], n);
			failed = 1;
			break;
		}
		if (n == 0)
			break;
		if (write((int)out, buf, (size_t)n) != n) {
			printf("cp: %s: error %ld\n", argv[2], -1L);
			failed = 1;
			break;
		}
	}

	close((int)in);
	close((int)out);
	return failed;
}

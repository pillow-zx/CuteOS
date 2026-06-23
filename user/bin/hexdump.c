#include <ulib.h>

static void print_hex_digit(unsigned int v)
{
	char c = v < 10 ? '0' + v : 'a' + v - 10;

	write(1, &c, 1);
}

static void print_hex_byte(unsigned char v)
{
	print_hex_digit(v >> 4);
	print_hex_digit(v & 0xf);
}

static void print_hex_offset(unsigned long v)
{
	for (int i = 7; i >= 0; i--)
		print_hex_digit((unsigned int)((v >> (i * 4)) & 0xf));
}

static int is_print(unsigned char c)
{
	return c >= 32 && c < 127;
}

static void dump_line(unsigned long off, const unsigned char *buf, long n)
{
	print_hex_offset(off);
	printf("  ");
	for (int i = 0; i < 16; i++) {
		if (i < n)
			print_hex_byte(buf[i]);
		else
			printf("  ");
		if ((i & 1) == 1)
			printf(" ");
	}
	printf(" |");
	for (int i = 0; i < n; i++) {
		char c = is_print(buf[i]) ? (char)buf[i] : '.';

		write(1, &c, 1);
	}
	printf("|\n");
}

static int dump_fd(int fd, const char *name)
{
	unsigned char buf[16];
	unsigned long off = 0;

	while (1) {
		long n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			printf("hexdump: %s: %s\n", name ? name : "-",
			       strerror(n));
			return 1;
		}
		if (n == 0)
			break;
		dump_line(off, buf, n);
		off += (unsigned long)n;
	}
	print_hex_offset(off);
	printf("\n");
	return 0;
}

static int dump_path(const char *path)
{
	long fd = open(path, O_RDONLY);
	int ret;

	if (fd < 0) {
		printf("hexdump: %s: %s\n", path, strerror(fd));
		return 1;
	}
	ret = dump_fd((int)fd, path);
	close((int)fd);
	return ret;
}

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc == 1)
		return dump_fd(0, "-");

	for (int i = 1; i < argc; i++) {
		if (streq(argv[i], "-")) {
			if (dump_fd(0, "-") != 0)
				failed = 1;
		} else if (dump_path(argv[i]) != 0) {
			failed = 1;
		}
	}
	return failed;
}

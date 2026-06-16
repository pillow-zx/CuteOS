#include <ulib.h>

int main(int argc, char **argv)
{
	char buf[512];
	long ret;

	(void)argc;
	(void)argv;

	ret = getcwd(buf, sizeof(buf));
	if (ret < 0) {
		print_error("pwd", NULL, ret);
		return 1;
	}
	print(buf);
	print("\n");
	return 0;
}

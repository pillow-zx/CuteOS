#include <ulib.h>

int main(int argc, char **argv)
{
	char buf[512];
	long ret;

	if (argc > 2 || (argc == 2 && !streq(argv[1], "--"))) {
		printf("usage: pwd\n");
		return 1;
	}
	(void)argv;

	ret = getcwd(buf, sizeof(buf));
	if (ret < 0) {
		printf("pwd: %s\n", strerror(ret));
		return 1;
	}
	printf("%s\n", buf);
	return 0;
}

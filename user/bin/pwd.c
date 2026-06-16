#include <ulib.h>

int main(int argc, char **argv)
{
	char buf[512];
	long ret;

	(void)argc;
	(void)argv;

	ret = getcwd(buf, sizeof(buf));
	if (ret < 0) {
		printf("pwd: error %ld\n", ret);
		return 1;
	}
	printf("%s\n", buf);
	return 0;
}

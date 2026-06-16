#include <ulib.h>

int main(int argc, char **argv)
{
	struct utsname uts;
	long ret;

	(void)argc;
	(void)argv;

	ret = uname(&uts);
	if (ret < 0) {
		printf("uname: error %ld\n", ret);
		return 1;
	}

	printf("%s %s %s\n", uts.sysname, uts.release, uts.machine);
	return 0;
}

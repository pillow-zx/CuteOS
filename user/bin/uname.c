#include <ulib.h>

int main(int argc, char **argv)
{
	struct utsname uts;
	long ret;
	int all = 0;

	if (argc > 2) {
		printf("usage: uname [-a]\n");
		return 1;
	}
	if (argc == 2) {
		if (streq(argv[1], "-a"))
			all = 1;
		else {
			printf("usage: uname [-a]\n");
			return 1;
		}
	}

	ret = uname(&uts);
	if (ret < 0) {
		printf("uname: error %ld\n", ret);
		return 1;
	}

	if (all)
		printf("%s %s %s %s %s\n", uts.sysname, uts.nodename,
		       uts.release, uts.version, uts.machine);
	else
		printf("%s %s %s\n", uts.sysname, uts.release, uts.machine);
	return 0;
}

#include <ulib.h>

int main(int argc, char **argv)
{
	struct utsname uts;
	long ret;

	(void)argc;
	(void)argv;

	ret = uname(&uts);
	if (ret < 0) {
		print_error("uname", NULL, ret);
		return 1;
	}

	print(uts.sysname);
	print(" ");
	print(uts.release);
	print(" ");
	print(uts.machine);
	print("\n");
	return 0;
}

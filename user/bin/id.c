#include <ulib.h>

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	print("uid=");
	print_dec((unsigned long)getuid());
	print(" gid=");
	print_dec((unsigned long)getgid());
	print("\n");
	return 0;
}

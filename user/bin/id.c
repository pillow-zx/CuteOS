#include <ulib.h>

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	printf("uid=%lu gid=%lu\n", (unsigned long)getuid(),
	       (unsigned long)getgid());
	return 0;
}

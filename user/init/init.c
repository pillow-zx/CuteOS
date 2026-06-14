#include <ulib.h>

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;

	char *sh_argv[] = { "sh", 0 };
	char *sh_envp[] = { "PATH=/bin", 0 };
	long ret;

	ret = execve("/bin/sh", sh_argv, sh_envp);
	print("init: exec /bin/sh failed, ret=");
	print_long(ret);
	print("\n");

	while (1) {
		yield();
	}

	return 1;
}

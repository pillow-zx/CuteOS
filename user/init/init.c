#include <ulib.h>

static long spawn_shell(void)
{
	char *sh_argv[] = { "sh", 0 };
	char *sh_envp[] = { "PATH=/bin", 0 };
	long pid = fork();

	if (pid == 0) {
		long ret = execve("/bin/sh", sh_argv, sh_envp);

		print("init: exec /bin/sh failed, ret=");
		print_long(ret);
		print("\n");
		exit(127);
	}

	if (pid < 0) {
		print("init: fork /bin/sh failed, ret=");
		print_long(pid);
		print("\n");
		return pid;
	}

	print("init: started /bin/sh pid=");
	print_long(pid);
	print("\n");
	return pid;
}

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;

	long shell_pid = -1;

	while (1) {
		int status = 0;
		long waited;

		if (shell_pid < 0) {
			shell_pid = spawn_shell();
			if (shell_pid < 0) {
				yield();
				continue;
			}
		}

		waited = wait(&status);
		if (waited < 0) {
			print("init: wait failed, ret=");
			print_long(waited);
			print("\n");
			yield();
			continue;
		}

		print("init: reaped pid=");
		print_long(waited);
		print(" status=");
		print_long(status);
		print("\n");

		if (waited == shell_pid)
			shell_pid = -1;
	}

	return 1;
}

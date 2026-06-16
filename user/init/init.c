#include <ulib.h>

static long spawn_shell(void)
{
	char *sh_argv[] = { "sh", 0 };
	char *sh_envp[] = { "PATH=/bin", 0 };
	long pid = fork();

	if (pid == 0) {
		long ret = execve("/bin/sh", sh_argv, sh_envp);

		printf("init: exec /bin/sh failed, ret=%ld\n", ret);
		exit(127);
	}

	if (pid < 0) {
		printf("init: fork /bin/sh failed, ret=%ld\n", pid);
		return pid;
	}

	printf("init: started /bin/sh pid=%ld\n", pid);
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
			printf("init: wait failed, ret=%ld\n", waited);
			yield();
			continue;
		}

		printf("init: reaped pid=%ld status=%ld\n",
		       waited, (long)status);

		if (waited == shell_pid)
			shell_pid = -1;
	}

	return 1;
}

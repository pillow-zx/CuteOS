#include <ulib.h>

int main(int argc, char **argv)
{
	int sig = SIGTERM;
	long pid;
	long ret;

	if (argc == 3 && argv[1][0] == '-') {
		sig = (int)atoi(argv[1] + 1);
		pid = atoi(argv[2]);
	} else if (argc == 2) {
		pid = atoi(argv[1]);
	} else {
		printf("usage: kill [-SIGNAL] PID\n");
		return 1;
	}

	ret = kill(pid, sig);
	if (ret < 0) {
		printf("kill: %s: error %ld\n", argv[argc - 1], ret);
		return 1;
	}
	return 0;
}

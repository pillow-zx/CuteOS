#include <ulib.h>

struct signal_name {
	int sig;
	const char *name;
};

static const struct signal_name signal_names[] = {
	{SIGHUP, "HUP"},   {SIGINT, "INT"},   {SIGQUIT, "QUIT"},
	{SIGILL, "ILL"},   {SIGTRAP, "TRAP"}, {SIGABRT, "ABRT"},
	{SIGBUS, "BUS"},   {SIGFPE, "FPE"},   {SIGKILL, "KILL"},
	{SIGUSR1, "USR1"}, {SIGSEGV, "SEGV"}, {SIGUSR2, "USR2"},
	{SIGPIPE, "PIPE"}, {SIGALRM, "ALRM"}, {SIGTERM, "TERM"},
	{SIGCHLD, "CHLD"}, {SIGCONT, "CONT"}, {SIGSTOP, "STOP"},
	{SIGSYS, "SYS"},
};

static void print_signal_list(void)
{
	for (size_t i = 0; i < sizeof(signal_names) / sizeof(signal_names[0]);
	     i++) {
		printf("%d:%s", signal_names[i].sig, signal_names[i].name);
		if (i + 1 < sizeof(signal_names) / sizeof(signal_names[0]))
			printf(" ");
	}
	printf("\n");
}

int main(int argc, char **argv)
{
	int sig = SIGTERM;
	long pid;
	long ret;

	if (argc == 2 && streq(argv[1], "-l")) {
		print_signal_list();
		return 0;
	}
	if (argc == 3 && argv[1][0] == '-' && argv[1][1] != '\0') {
		sig = (int)atoi(argv[1] + 1);
		pid = atoi(argv[2]);
	} else if (argc == 2) {
		pid = atoi(argv[1]);
	} else {
		printf("usage: kill [-SIGNAL] PID\n");
		printf("       kill -l\n");
		return 1;
	}

	ret = kill(pid, sig);
	if (ret < 0) {
		printf("kill: %s: error %ld\n", argv[argc - 1], ret);
		return 1;
	}
	return 0;
}

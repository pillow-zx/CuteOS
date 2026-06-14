#include <ulib.h>

#define LINE_MAX 80
#define ARGV_MAX 8

static void print_prompt(void)
{
	print("cuteos$ ");
}

static int is_space(char ch)
{
	return ch == ' ' || ch == '\t';
}

static int is_printable(char ch)
{
	return ch >= ' ' && ch <= '~';
}

static int read_line(char *line, int size)
{
	int len = 0;

	while (1) {
		char ch;
		long n = read(0, &ch, 1);

		if (n < 0)
			return (int)n;
		if (n == 0)
			continue;

		if (ch == '\r' || ch == '\n') {
			print("\n");
			line[len] = '\0';
			return len;
		}

		if (ch == '\b' || ch == 0x7f) {
			if (len > 0) {
				len--;
				print("\b \b");
			}
			continue;
		}

		if (!is_printable(ch))
			continue;

		if (len + 1 < size) {
			line[len++] = ch;
			write(1, &ch, 1);
		}
	}
}

static int parse_line(char *line, char **argv)
{
	int argc = 0;
	char *p = line;

	while (*p && argc < ARGV_MAX - 1) {
		while (is_space(*p))
			p++;
		if (!*p)
			break;

		argv[argc++] = p;
		while (*p && !is_space(*p))
			p++;
		if (*p)
			*p++ = '\0';
	}

	argv[argc] = NULL;
	return argc;
}

static void build_path(char *dst, const char *cmd)
{
	if (cmd[0] == '/') {
		strcpy(dst, cmd);
		return;
	}

	strcpy(dst, "/bin/");
	strcpy(dst + 5, cmd);
}

static void run_command(int argc, char **argv)
{
	char path[64];
	char *envp[] = { "PATH=/bin", 0 };
	long pid;

	if (streq(argv[0], "help")) {
		print("commands: help, syscall-test, /path/to/program\n");
		return;
	}

	if (streq(argv[0], "exit")) {
		exit(0);
	}

	if (argv[0][0] != '/' && strlen(argv[0]) + 5 >= sizeof(path)) {
		print("command name too long\n");
		return;
	}
	if (argv[0][0] == '/' && strlen(argv[0]) >= sizeof(path)) {
		print("path too long\n");
		return;
	}

	build_path(path, argv[0]);
	pid = fork();
	if (pid == 0) {
		long ret = execve(path, argv, envp);

		print("exec failed: ");
		print(path);
		print(", ret=");
		print_long(ret);
		print("\n");
		exit(127);
	}

	if (pid < 0) {
		print("fork failed, ret=");
		print_long(pid);
		print("\n");
		return;
	}

	int status = -1;
	long waited = wait4(pid, &status, 0, 0);

	if (waited < 0) {
		print("wait failed, ret=");
		print_long(waited);
		print("\n");
		return;
	}

	print("[exit ");
	print_long(status);
	print("]\n");
	(void)argc;
}

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;

	print("CuteOS shell\n");

	while (1) {
		char line[LINE_MAX];
		char *cmd_argv[ARGV_MAX];
		int n;
		int cmd_argc;

		print_prompt();
		n = read_line(line, sizeof(line));
		if (n < 0) {
			print("read failed, ret=");
			print_long(n);
			print("\n");
			continue;
		}

		cmd_argc = parse_line(line, cmd_argv);
		if (cmd_argc == 0)
			continue;

		run_command(cmd_argc, cmd_argv);
	}

	return 0;
}

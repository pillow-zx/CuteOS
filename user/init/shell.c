#include <ulib.h>

#define LINE_MAX 128
#define ARGV_MAX 16

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
	static char pipe_tok[] = "|";

	while (*p && argc < ARGV_MAX - 1) {
		while (is_space(*p))
			p++;
		if (!*p)
			break;

		if (*p == '|') {
			argv[argc++] = pipe_tok;
			p++;
			continue;
		}

		argv[argc++] = p;
		while (*p && !is_space(*p) && *p != '|')
			p++;
		if (*p == '|') {
			*p++ = '\0';
			if (argc < ARGV_MAX - 1)
				argv[argc++] = pipe_tok;
		} else if (*p) {
			*p++ = '\0';
		}
	}

	argv[argc] = NULL;
	return argc;
}

static int build_path(char *dst, size_t size, const char *cmd)
{
	if (cmd[0] == '/') {
		if (strlen(cmd) >= size)
			return -1;
		strcpy(dst, cmd);
		return 0;
	}

	if (strlen(cmd) + 5 >= size)
		return -1;
	strcpy(dst, "/bin/");
	strcpy(dst + 5, cmd);
	return 0;
}

static void print_help(void)
{
	print("commands: cd help exit ls cat echo touch mkdir rmdir rm ");
	print("pwd cp stat uname id kill true false\n");
	print("pipe: cmd | cmd\n");
}

static void exec_child(char **argv)
{
	char path[64];
	char *envp[] = { "PATH=/bin", 0 };

	if (build_path(path, sizeof(path), argv[0]) < 0) {
		print("command path too long\n");
		exit(127);
	}

	long ret = execve(path, argv, envp);

	print("exec failed: ");
	print(path);
	print(", ret=");
	print_long(ret);
	print("\n");
	exit(127);
}

static int wait_and_report(long pid)
{
	int status = -1;
	long waited = wait4(pid, &status, 0, 0);

	if (waited < 0) {
		print("wait failed, ret=");
		print_long(waited);
		print("\n");
		return 1;
	}

	print("[exit ");
	print_long(status);
	print("]\n");
	return status == 0 ? 0 : 1;
}

static void run_external(char **argv)
{
	long pid = fork();

	if (pid == 0)
		exec_child(argv);

	if (pid < 0) {
		print("fork failed, ret=");
		print_long(pid);
		print("\n");
		return;
	}

	wait_and_report(pid);
}

static int find_pipe(char **argv)
{
	for (int i = 0; argv[i]; i++) {
		if (streq(argv[i], "|"))
			return i;
	}
	return -1;
}

static void run_pipeline(char **argv, int pipe_index)
{
	int pipefd[2];
	long left;
	long right;

	if (pipe_index == 0 || !argv[pipe_index + 1]) {
		print("invalid pipeline\n");
		return;
	}

	argv[pipe_index] = NULL;
	if (pipe(pipefd) != 0) {
		print("pipe failed\n");
		return;
	}

	left = fork();
	if (left == 0) {
		close(pipefd[0]);
		if (dup2(pipefd[1], 1) < 0)
			exit(126);
		close(pipefd[1]);
		exec_child(argv);
	}
	if (left < 0) {
		print("fork failed\n");
		close(pipefd[0]);
		close(pipefd[1]);
		return;
	}

	right = fork();
	if (right == 0) {
		close(pipefd[1]);
		if (dup2(pipefd[0], 0) < 0)
			exit(126);
		close(pipefd[0]);
		exec_child(&argv[pipe_index + 1]);
	}
	close(pipefd[0]);
	close(pipefd[1]);

	if (right < 0) {
		print("fork failed\n");
		wait_and_report(left);
		return;
	}

	wait_and_report(left);
	wait_and_report(right);
}

static void run_command(int argc, char **argv)
{
	int pipe_index;

	if (streq(argv[0], "help")) {
		print_help();
		return;
	}

	if (streq(argv[0], "exit")) {
		exit(0);
	}

	if (streq(argv[0], "cd")) {
		const char *path = argc > 1 ? argv[1] : "/";
		long ret = chdir(path);

		if (ret < 0) {
			print("cd: ");
			print(path);
			print(": ");
			print_long(ret);
			print("\n");
		}
		return;
	}

	pipe_index = find_pipe(argv);
	if (pipe_index >= 0) {
		run_pipeline(argv, pipe_index);
		return;
	}

	run_external(argv);
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

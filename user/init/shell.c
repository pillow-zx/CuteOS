#include <ulib.h>

#define LINE_MAX 256
#define ARGV_MAX 64
#define PIPE_MAX 16
#define PATH_MAX 512

struct command {
	char **argv;
	const char *input_path;
	const char *output_path;
	int append;
};

static void print_prompt(void)
{
	char cwd[PATH_MAX];
	long ret = getcwd(cwd, sizeof(cwd));

	if (ret < 0)
		printf("?");
	else
		printf("%s", cwd);
	printf("$ ");
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
			printf("\n");
			line[len] = '\0';
			return len;
		}

		if (ch == '\b' || ch == 0x7f) {
			if (len > 0) {
				len--;
				printf("\b \b");
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
	static char in_tok[] = "<";
	static char out_tok[] = ">";
	static char append_tok[] = ">>";

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
		if (*p == '<') {
			argv[argc++] = in_tok;
			p++;
			continue;
		}
		if (*p == '>') {
			if (p[1] == '>') {
				argv[argc++] = append_tok;
				p += 2;
			} else {
				argv[argc++] = out_tok;
				p++;
			}
			continue;
		}

		argv[argc++] = p;
		while (*p && !is_space(*p) && *p != '|' && *p != '<' &&
		       *p != '>')
			p++;
		if (*p == '|' || *p == '<' || *p == '>') {
			*p++ = '\0';
		} else if (*p) {
			*p++ = '\0';
		}
	}

	argv[argc] = NULL;
	return argc;
}

static int build_path(char *dst, size_t size, const char *cmd)
{
	if (cmd[0] == '/' || strchr(cmd, '/')) {
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
	printf("commands: cd help exit ls cat echo touch mkdir rmdir rm\n");
	printf("          pwd cp stat uname id kill true false\n");
	printf("options:  ls -l  rm -fr  cp -r  mkdir -p  echo -n\n");
	printf("pipeline: cmd1 | cmd2 | cmd3 ...\n");
}

static void exec_child(char **argv)
{
	char path[64];
	char *envp[] = {"PATH=/bin", 0};

	if (build_path(path, sizeof(path), argv[0]) < 0) {
		printf("command path too long\n");
		exit(127);
	}

	long ret = execve(path, argv, envp);

	printf("exec failed: %s, ret=%ld\n", path, ret);
	exit(127);
}

static int apply_redirections(const struct command *cmd)
{
	long fd;

	if (cmd->input_path) {
		fd = open(cmd->input_path, O_RDONLY);
		if (fd < 0) {
			printf("%s: error %ld\n", cmd->input_path, fd);
			return -1;
		}
		if (dup2((int)fd, 0) < 0) {
			close((int)fd);
			return -1;
		}
		close((int)fd);
	}

	if (cmd->output_path) {
		int flags = O_CREAT | O_WRONLY;

		flags |= cmd->append ? O_APPEND : O_TRUNC;
		fd = openat(AT_FDCWD, cmd->output_path, flags, 0666);
		if (fd < 0) {
			printf("%s: error %ld\n", cmd->output_path, fd);
			return -1;
		}
		if (dup2((int)fd, 1) < 0) {
			close((int)fd);
			return -1;
		}
		close((int)fd);
	}

	return 0;
}

static int wait_and_report(long pid)
{
	int status = -1;
	long waited = wait4(pid, &status, 0, 0);

	if (waited < 0) {
		printf("wait failed, ret=%ld\n", waited);
		return 1;
	}

	printf("[exit %ld]\n", (long)status);
	return status == 0 ? 0 : 1;
}

static int parse_redirections(char **argv, struct command *cmd)
{
	int argc = 0;

	cmd->argv = argv;
	cmd->input_path = NULL;
	cmd->output_path = NULL;
	cmd->append = 0;

	for (int i = 0; argv[i]; i++) {
		if (streq(argv[i], "<")) {
			if (!argv[i + 1] || cmd->input_path)
				return -1;
			cmd->input_path = argv[++i];
			continue;
		}
		if (streq(argv[i], ">")) {
			if (!argv[i + 1] || cmd->output_path)
				return -1;
			cmd->output_path = argv[++i];
			cmd->append = 0;
			continue;
		}
		if (streq(argv[i], ">>")) {
			if (!argv[i + 1] || cmd->output_path)
				return -1;
			cmd->output_path = argv[++i];
			cmd->append = 1;
			continue;
		}
		argv[argc++] = argv[i];
	}

	argv[argc] = NULL;
	return argc > 0 ? 0 : -1;
}

static void run_external(struct command *cmd)
{
	long pid = fork();

	if (pid == 0) {
		if (apply_redirections(cmd) < 0)
			exit(126);
		exec_child(cmd->argv);
	}

	if (pid < 0) {
		printf("fork failed, ret=%ld\n", pid);
		return;
	}

	wait_and_report(pid);
}

static int split_pipeline(char **argv, char ***cmdv)
{
	int count = 0;

	if (!argv[0])
		return -1;

	cmdv[count++] = argv;
	for (int i = 0; argv[i]; i++) {
		if (!streq(argv[i], "|"))
			continue;
		if (i == 0 || !argv[i + 1] || count >= PIPE_MAX)
			return -1;
		argv[i] = NULL;
		cmdv[count++] = &argv[i + 1];
	}

	for (int i = 0; i < count; i++) {
		if (!cmdv[i][0])
			return -1;
	}

	return count;
}

static void close_pipe_pair(int pipefd[2])
{
	close(pipefd[0]);
	close(pipefd[1]);
}

static void run_pipeline(char **argv)
{
	char **cmdv[PIPE_MAX];
	struct command cmds[PIPE_MAX];
	long pids[PIPE_MAX];
	int ncmd = split_pipeline(argv, cmdv);
	int prev_read = -1;
	int failed = 0;

	if (ncmd < 2) {
		printf("invalid pipeline\n");
		return;
	}

	for (int i = 0; i < ncmd; i++) {
		if (parse_redirections(cmdv[i], &cmds[i]) < 0) {
			printf("invalid redirection\n");
			return;
		}
	}

	for (int i = 0; i < ncmd; i++) {
		int pipefd[2] = {-1, -1};
		long pid;

		if (i + 1 < ncmd && pipe(pipefd) != 0) {
			printf("pipe failed\n");
			if (prev_read >= 0)
				close(prev_read);
			return;
		}

		pid = fork();
		if (pid == 0) {
			if (prev_read >= 0) {
				if (dup2(prev_read, 0) < 0)
					exit(126);
				close(prev_read);
			}
			if (pipefd[1] >= 0) {
				if (dup2(pipefd[1], 1) < 0)
					exit(126);
				close_pipe_pair(pipefd);
			}
			if (apply_redirections(&cmds[i]) < 0)
				exit(126);
			exec_child(cmds[i].argv);
		}

		if (pid < 0) {
			printf("fork failed\n");
			if (prev_read >= 0)
				close(prev_read);
			if (pipefd[0] >= 0)
				close_pipe_pair(pipefd);
			return;
		}

		pids[i] = pid;
		if (prev_read >= 0)
			close(prev_read);
		if (pipefd[1] >= 0) {
			close(pipefd[1]);
			prev_read = pipefd[0];
		} else {
			prev_read = -1;
		}
	}

	if (prev_read >= 0)
		close(prev_read);

	for (int i = 0; i < ncmd; i++)
		failed |= wait_and_report(pids[i]);
	(void)failed;
}

static void run_command(int argc, char **argv)
{
	struct command cmd;
	int has_pipe = 0;

	(void)argc;

	for (int i = 0; argv[i]; i++) {
		if (streq(argv[i], "|")) {
			has_pipe = 1;
			break;
		}
	}

	if (has_pipe) {
		run_pipeline(argv);
		return;
	}

	if (parse_redirections(argv, &cmd) < 0) {
		printf("invalid redirection\n");
		return;
	}

	if (streq(cmd.argv[0], "help")) {
		if (cmd.input_path || cmd.output_path) {
			printf("help: redirection not supported for builtin\n");
			return;
		}
		print_help();
		return;
	}

	if (streq(cmd.argv[0], "exit")) {
		if (cmd.input_path || cmd.output_path) {
			printf("exit: redirection not supported for builtin\n");
			return;
		}
		exit(0);
	}

	if (streq(cmd.argv[0], "cd")) {
		const char *path = cmd.argv[1] ? cmd.argv[1] : "/";
		long ret = chdir(path);

		if (ret < 0)
			printf("cd: %s: %ld\n", path, ret);
		return;
	}

	run_external(&cmd);
}

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;

	printf("CuteOS shell\n");

	while (1) {
		char line[LINE_MAX];
		char *cmd_argv[ARGV_MAX];
		int n;
		int cmd_argc;

		print_prompt();
		n = read_line(line, sizeof(line));
		if (n < 0) {
			printf("read failed, ret=%ld\n", (long)n);
			continue;
		}

		cmd_argc = parse_line(line, cmd_argv);
		if (cmd_argc == 0)
			continue;

		run_command(cmd_argc, cmd_argv);
	}

	return 0;
}

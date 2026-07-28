#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <utest.h>

static void runtime_exec_probe(const char *probe, char *const argv[],
			       char *const envp[])
{
	char *const empty_env[] = {
		NULL,
	};
	pid_t child = UT_FORK();

	if (child == 0) {
		if (!envp)
			envp = empty_env;
		execve(probe, argv, envp);
		_exit(127);
	}
	UT_EXPECT_EXIT(child, 0);
}

UT_CASE(runtime_exec_argv_env, 5000)
{
	char *probe = ut_exec_path("utest-probe-argv-env");
	char *output = ut_path("argv-env.out");
	char *content;
	char *const argv[] = {
		probe,
		output,
		"alpha",
		"beta",
		NULL,
	};
	char *const envp[] = {
		"UTEST_EXEC_TOKEN=runtime-token",
		"PATH=/bin:/sbin",
		NULL,
	};

	UT_ASSERT(probe != NULL);
	UT_ASSERT(output != NULL);
	runtime_exec_probe(probe, argv, envp);
	content = ut_read_file("argv-env.out", NULL);
	UT_ASSERT(content != NULL);
	UT_EXPECT_STREQ(content, "alpha|beta|runtime-token\n");
	free(content);
	free(output);
	free(probe);
}

UT_CASE(runtime_exec_bss, 5000)
{
	char *probe = ut_exec_path("utest-probe-bss");
	char *output = ut_path("bss.out");
	char *content;
	char *const argv[] = {
		probe,
		output,
		NULL,
	};

	UT_ASSERT(probe != NULL);
	UT_ASSERT(output != NULL);
	runtime_exec_probe(probe, argv, NULL);
	content = ut_read_file("bss.out", NULL);
	UT_ASSERT(content != NULL);
	UT_EXPECT_STREQ(content, "zero\n");
	free(content);
	free(output);
	free(probe);
}

UT_CASE(runtime_exec_initial_tls, 5000)
{
	char *probe = ut_exec_path("utest-probe-tls");
	char *output = ut_path("tls.out");
	char *content;
	char *const argv[] = {
		probe,
		output,
		NULL,
	};

	UT_ASSERT(probe != NULL);
	UT_ASSERT(output != NULL);
	runtime_exec_probe(probe, argv, NULL);
	content = ut_read_file("tls.out", NULL);
	UT_ASSERT(content != NULL);
	UT_EXPECT_STREQ(content, "isolated\n");
	free(content);
	free(output);
	free(probe);
}

UT_CASE(runtime_errno, 1500)
{
	UT_ASSERT_ERRNO(open("does-not-exist", O_RDONLY), ENOENT);
	UT_EXPECT_ERRNO(close(-1), EBADF);
}

UT_CASE(runtime_cloexec_survives_exec, 5000)
{
	char *path = ut_path("cloexec.file");
	char *probe = ut_exec_path("utest-probe-cloexec");
	char descriptor[32];
	char *const argv[] = {
		probe,
		descriptor,
		NULL,
	};
	int fd;

	UT_ASSERT(path != NULL);
	UT_ASSERT(probe != NULL);
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
	UT_ASSERT(fd >= 0);
	UT_ASSERT_EQ(fcntl(fd, F_SETFD, FD_CLOEXEC), 0);
	(void)snprintf(descriptor, sizeof(descriptor), "%d", fd);
	runtime_exec_probe(probe, argv, NULL);
	UT_EXPECT_EQ(close(fd), 0);
	free(probe);
	free(path);
}

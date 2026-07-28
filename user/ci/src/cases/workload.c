#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <utest.h>

UT_CASE(workload_busybox_init_ash_and_file_flow, 20000)
{
	char *content;
	static const char script[] =
		"mkdir workload\n"
		"printf 'pipeline-data\\n' > workload/source\n"
		"cat workload/source | cat > workload/pipe\n"
		"cp workload/pipe workload/copy\n"
		"mv workload/copy workload/final\n"
		"stat workload/final > workload/stat\n"
		"test -s workload/final\n";
	char *const argv[] = {
		"/bin/sh",
		"workload.sh",
		NULL,
	};
	char *const envp[] = {
		"PATH=/bin:/sbin",
		"HOME=/",
		NULL,
	};
	pid_t child;

	UT_ASSERT_EQ(ut_write_file("workload.sh", script, sizeof(script) - 1,
				   0700), 0);
	child = UT_FORK();
	if (child == 0) {
		execve("/bin/sh", argv, envp);
		dprintf(STDERR_FILENO, "[UTEST] exec /bin/sh failed: errno=%d\n",
			errno);
		_exit(127);
	}
	UT_EXPECT_EXIT(child, 0);
	content = ut_read_file("workload/final", NULL);
	UT_ASSERT(content != NULL);
	UT_EXPECT_STREQ(content, "pipeline-data\n");
	free(content);
	sync();
}

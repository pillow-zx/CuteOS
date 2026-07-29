#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <utest.h>

static void workload_run_shell(const char *script)
{
	char *const argv[] = {
		"sh",
		"busybox-workload.sh",
		NULL,
	};
	char *const envp[] = {
		"PATH=/bin:/sbin",
		"HOME=/",
		NULL,
	};
	pid_t child;

	UT_ASSERT_EQ(ut_write_file("busybox-workload.sh", script,
				   strlen(script), 0700),
		     0);
	child = UT_FORK();

	if (child == 0) {
		execve("/bin/sh", argv, envp);
		dprintf(STDERR_FILENO,
			"[UTEST] exec /bin/sh failed: errno=%d\n", errno);
		_exit(127);
	}
	UT_EXPECT_EXIT(child, 0);
}

static void workload_expect_file(const char *path, const void *expected,
				 size_t expected_size)
{
	char *actual;
	size_t actual_size;

	actual = ut_read_file(path, &actual_size);
	UT_ASSERT(actual != NULL);
	UT_ASSERT_EQ(actual_size, expected_size);
	UT_EXPECT_MEMEQ(actual, expected, expected_size);
	free(actual);
}

UT_CASE(workload_busybox_init_ash_and_file_flow, 20000)
{
	char *content;
	static const char script[] =
		"mkdir workload\n"
		"printf 'pipeline-data\\n' > workload/source\n"
		"cat workload/source | cat | cat > workload/pipe\n"
		"cat < workload/source > workload/redirected\n"
		"cat <<'EOF' | cat > workload/here-pipe\n"
		"here-pipe-data\n"
		"EOF\n"
		"(printf 'background-data\\n' > workload/background) &\n"
		"wait\n"
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

	UT_ASSERT_EQ(
		ut_write_file("workload.sh", script, sizeof(script) - 1, 0700),
		0);
	child = UT_FORK();
	if (child == 0) {
		execve("/bin/sh", argv, envp);
		dprintf(STDERR_FILENO,
			"[UTEST] exec /bin/sh failed: errno=%d\n", errno);
		_exit(127);
	}
	UT_EXPECT_EXIT(child, 0);
	content = ut_read_file("workload/final", NULL);
	UT_ASSERT(content != NULL);
	UT_EXPECT_STREQ(content, "pipeline-data\n");
	free(content);
	content = ut_read_file("workload/redirected", NULL);
	UT_ASSERT(content != NULL);
	UT_EXPECT_STREQ(content, "pipeline-data\n");
	free(content);
	content = ut_read_file("workload/here-pipe", NULL);
	UT_ASSERT(content != NULL);
	UT_EXPECT_STREQ(content, "here-pipe-data\n");
	free(content);
	content = ut_read_file("workload/background", NULL);
	UT_ASSERT(content != NULL);
	UT_EXPECT_STREQ(content, "background-data\n");
	free(content);
	sync();
}

UT_CASE(workload_busybox_ash_blocking_wait, 5000)
{
	static const char script[] = "sleep 1 &\n"
				     "wait\n";

	workload_run_shell(script);
}

UT_CASE(workload_busybox_pathname_applets, 5000)
{
	static const char script[] =
		"set -eu\n"
		"mkdir paths paths/dir\n"
		"printf original > paths/dir/file\n"
		"basename paths/dir/file > basename.out\n"
		"test \"$(cat basename.out)\" = file\n"
		"ln paths/dir/file paths/hard\n"
		"printf updated > paths/hard\n"
		"test \"$(cat paths/dir/file)\" = updated\n"
		"ln -s dir/file paths/relative\n"
		"ln -s missing-target paths/dangling\n"
		"test \"$(readlink paths/relative)\" = dir/file\n"
		"test \"$(readlink paths/dangling)\" = missing-target\n"
		"test \"$(realpath paths/relative)\" = "
		"\"$(pwd)/paths/dir/file\"\n"
		"realpath paths/dangling > dangling.out\n";

	workload_run_shell(script);
}

UT_CASE(workload_busybox_streaming_text_applets, 5000)
{
	static const char script[] =
		"set -eu\n"
		"printf 'red 1\\nblue 2\\nred 3\\n' > text\n"
		"printf '1\\n3\\n' > awk.expected\n"
		"awk '/^red/ { print $2 }' text > awk.out\n"
		"cmp awk.expected awk.out\n"
		"cat text | grep '^red' | sed 's/^red/item/' > stream.out\n"
		"printf 'item 1\\nitem 3\\n' > stream.expected\n"
		"cmp stream.expected stream.out\n"
		"diff stream.expected stream.out\n"
		"if cmp stream.expected text > cmp-different.out 2> "
		"cmp-different.err; "
		"then exit 1; fi\n"
		"if diff stream.expected text > diff-different.out 2> "
		"diff-different.err; "
		"then exit 1; fi\n"
		"printf 'alpha\\nbeta\\n' | xargs -n 1 echo > xargs.out\n"
		"printf 'alpha\\nbeta\\n' > xargs.expected\n"
		"cmp xargs.expected xargs.out\n";

	workload_run_shell(script);
}

UT_CASE(workload_busybox_filesystem_applets, 5000)
{
	static const char script[] =
		"set -eu\n"
		"mkdir tree tree/nested\n"
		"printf leaf > tree/nested/leaf\n"
		"ln -s nested tree/relative\n"
		"find tree > find.out\n"
		"grep -qx 'tree/nested/leaf' find.out\n"
		"grep -qx 'tree/relative' find.out\n"
		"if grep -q 'tree/relative/leaf' find.out; then exit 1; fi\n"
		"find tree -type d > find-dirs.out\n"
		"grep -qx 'tree' find-dirs.out\n"
		"grep -qx 'tree/nested' find-dirs.out\n"
		"if grep -q 'tree/relative' find-dirs.out; then exit 1; fi\n"
		"find tree -type f > find-files.out\n"
		"grep -qx 'tree/nested/leaf' find-files.out\n"
		"if grep -q 'tree/relative' find-files.out; then exit 1; fi\n"
		"find tree -type l > find-links.out\n"
		"grep -qx 'tree/relative' find-links.out\n"
		"du -s tree > du.out\n"
		"awk 'NF >= 2 && $1 > 0 { found = 1 } "
		"END { exit !found }' du.out\n";

	workload_run_shell(script);
}

UT_CASE(workload_busybox_io_applets, 5000)
{
	struct stat st;
	static const char script[] =
		"set -eu\n"
		"printf abcdef > input\n"
		"dd if=input of=offset bs=2 seek=1 count=2 > dd.out 2> dd.err\n"
		"fallocate -o 8 -l 8 allocation\n"
		"printf abc > extended\n"
		"truncate -s 16 extended\n"
		"printf abcdef > shrunk\n"
		"truncate -s 3 shrunk\n"
		"sync\n";

	workload_run_shell(script);
	workload_expect_file("offset", "\0\0abcd", 6);
	UT_ASSERT_EQ(stat("allocation", &st), 0);
	UT_EXPECT_EQ(st.st_size, 16);
	workload_expect_file("extended", "abc\0\0\0\0\0\0\0\0\0\0\0\0\0", 16);
	workload_expect_file("shrunk", "abc", 3);
}

UT_CASE(workload_busybox_stty_applets, 5000)
{
	char *termios;
	static const char script[] =
		"set -eu\n"
		"saved=$(stty -g < /dev/console)\n"
		"set -- $(stty size < /dev/console)\n"
		"saved_rows=$1\n"
		"saved_cols=$2\n"
		"restore() {\n"
		"  stty \"$saved\" < /dev/console\n"
		"  stty rows \"$saved_rows\" cols \"$saved_cols\" < "
		"/dev/console\n"
		"}\n"
		"trap restore EXIT\n"
		"stty -echo < /dev/console\n"
		"stty -a < /dev/console > termios.out\n"
		"stty rows 41 cols 119 < /dev/console\n"
		"stty size < /dev/console > winsize.out\n";

	workload_run_shell(script);
	termios = ut_read_file("termios.out", NULL);
	UT_ASSERT(termios != NULL);
	UT_EXPECT(strstr(termios, "-echo") != NULL);
	free(termios);
	workload_expect_file("winsize.out", "41 119\n", 7);
}

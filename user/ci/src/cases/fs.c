#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <utest.h>

static bool fs_directory_contains(const char *name)
{
	DIR *directory = opendir(".");
	struct dirent *entry;
	bool found = false;

	if (!directory)
		UT_FAIL("opendir failed: errno=%d", errno);
	while ((entry = readdir(directory)) != NULL) {
		if (strcmp(entry->d_name, name) == 0) {
			found = true;
			break;
		}
	}
	UT_ASSERT_EQ(closedir(directory), 0);
	return found;
}

UT_CASE(fs_cwd_dirfd_at, 1500)
{
	char before[4096];
	char after[4096];
	int dirfd;
	int fd;

	UT_ASSERT(getcwd(before, sizeof(before)) != NULL);
	UT_ASSERT_EQ(ut_mkdir("dir", 0700), 0);
	dirfd = open("dir", O_RDONLY | O_DIRECTORY);
	UT_ASSERT(dirfd >= 0);
	fd = openat(dirfd, "child", O_RDWR | O_CREAT | O_EXCL, 0600);
	UT_ASSERT(fd >= 0);
	UT_ASSERT_EQ(write(fd, "at", 2), 2);
	UT_ASSERT_EQ(close(fd), 0);
	UT_ASSERT_EQ(chdir("dir"), 0);
	UT_ASSERT(getcwd(after, sizeof(after)) != NULL);
	UT_EXPECT_NE(strcmp(before, after), 0);
	UT_EXPECT_ERRNO(openat(dirfd, "missing", O_RDONLY), ENOENT);
	UT_EXPECT_EQ(chdir(before), 0);
	UT_EXPECT_EQ(close(dirfd), 0);
}

UT_CASE(fs_links_rename_and_state, 1500)
{
	char link_target[32] = {};
	char *source_content;
	char *target_content;

	UT_ASSERT_EQ(ut_write_file("source", "source-data", 11, 0600), 0);
	UT_ASSERT_EQ(ut_write_file("target", "target-data", 11, 0600), 0);
	UT_ASSERT_EQ(symlinkat("source", AT_FDCWD, "symbolic"), 0);
	UT_ASSERT_EQ(linkat(AT_FDCWD, "source", AT_FDCWD, "hard", 0), 0);
	UT_ASSERT_EQ(readlink("symbolic", link_target, sizeof(link_target)), 6);
	UT_EXPECT_STREQ(link_target, "source");
	UT_ASSERT_ERRNO(syscall(SYS_renameat2, AT_FDCWD, "source", AT_FDCWD,
				    "target", RENAME_NOREPLACE), EEXIST);
	source_content = ut_read_file("source", NULL);
	target_content = ut_read_file("target", NULL);
	UT_ASSERT(source_content != NULL);
	UT_ASSERT(target_content != NULL);
	UT_EXPECT_STREQ(source_content, "source-data");
	UT_EXPECT_STREQ(target_content, "target-data");
	free(target_content);
	free(source_content);
	UT_EXPECT(fs_directory_contains("hard"));
	UT_EXPECT(fs_directory_contains("symbolic"));
}

UT_CASE(fs_stat_umask_and_reopen, 1500)
{
	struct stat st;
	mode_t old_umask;
	char *content;
	char *path;
	int fd;

	old_umask = umask(0077);
	path = ut_path("private");
	UT_ASSERT(path != NULL);
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
	free(path);
	UT_ASSERT(fd >= 0);
	UT_ASSERT_EQ(write(fd, "persist", 7), 7);
	UT_ASSERT_EQ(close(fd), 0);
	umask(old_umask);
	UT_ASSERT_EQ(stat("private", &st), 0);
	UT_EXPECT_EQ(st.st_mode & 0777, 0600);
	content = ut_read_file("private", NULL);
	UT_ASSERT(content != NULL);
	UT_EXPECT_STREQ(content, "persist");
	free(content);
}

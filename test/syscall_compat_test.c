#include <kernel/fs.h>
#include <kernel/resource.h>
#include <kernel/statfs.h>
#include <kernel/test.h>
#include <kernel/vfs.h>

void test_rlimit_defaults(void)
{
	struct rlimit64 limits[RLIM_NLIMITS];

	TEST_BEGIN("syscall compat: rlimit defaults");
	{
		rlimits_init(limits);
		TEST_ASSERT_EQ(limits[RLIMIT_NOFILE].rlim_cur, (uint64_t)NR_OPEN);
		TEST_ASSERT_EQ(limits[RLIMIT_NOFILE].rlim_max, (uint64_t)NR_OPEN);
		TEST_ASSERT_EQ(limits[RLIMIT_AS].rlim_cur,
			       (uint64_t)RLIM_INFINITY);
	}
	TEST_END("syscall compat: rlimit defaults");
	return;
fail:
	TEST_FAIL("syscall compat: rlimit defaults", "see above");
}

void test_vfs_default_poll_masks(void)
{
	struct file file = {
		.f_mode = FMODE_READ | FMODE_WRITE,
	};

	TEST_BEGIN("syscall compat: default poll masks");
	{
		TEST_ASSERT_EQ(vfs_poll(&file, POLLIN), (uint32_t)POLLIN);
		TEST_ASSERT_EQ(vfs_poll(&file, POLLOUT), (uint32_t)POLLOUT);
		TEST_ASSERT_EQ(vfs_poll(&file, POLLIN | POLLOUT),
			       (uint32_t)(POLLIN | POLLOUT));
		TEST_ASSERT_EQ(vfs_poll(NULL, POLLIN), (uint32_t)POLLNVAL);
	}
	TEST_END("syscall compat: default poll masks");
	return;
fail:
	TEST_FAIL("syscall compat: default poll masks", "see above");
}

void test_root_statfs_fields(void)
{
	struct kstatfs st;

	TEST_BEGIN("syscall compat: root statfs fields");
	{
		TEST_ASSERT_NOT_NULL(root_dentry);
		TEST_ASSERT_NOT_NULL(root_dentry->d_sb);
		TEST_ASSERT_EQ(vfs_statfs(root_dentry->d_sb, &st), 0);
		TEST_ASSERT_EQ(st.f_type, (int64_t)0xef53);
		TEST_ASSERT(st.f_bsize > 0);
		TEST_ASSERT(st.f_blocks > 0);
		TEST_ASSERT(st.f_namelen >= 255);
	}
	TEST_END("syscall compat: root statfs fields");
	return;
fail:
	TEST_FAIL("syscall compat: root statfs fields", "see above");
}

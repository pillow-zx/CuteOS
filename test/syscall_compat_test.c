#include <kernel/fs.h>
#include <kernel/errno.h>
#include <kernel/pipe.h>
#include <kernel/resource.h>
#include <kernel/signal.h>
#include <kernel/statfs.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/test.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>
#include <uapi/tty.h>

ssize_t console_tty_read_stream_for_test(const struct termios *termios,
					 const char *input, size_t input_len,
					 char *out, size_t out_size, char *echo,
					 size_t echo_size, int *signal);
ssize_t console_tty_write_for_test(const struct termios *termios,
				   const char *input, size_t input_len,
				   char *out, size_t out_size);

void test_rlimit_defaults(void)
{
	struct rlimit64 limits[RLIM_NLIMITS];

	TEST_BEGIN("syscall compat: rlimit defaults");
	{
		rlimits_init(limits);
		TEST_ASSERT_EQ(limits[RLIMIT_NOFILE].rlim_cur,
			       (uint64_t)NR_OPEN);
		TEST_ASSERT_EQ(limits[RLIMIT_NOFILE].rlim_max,
			       (uint64_t)NR_OPEN);
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

void test_vfs_default_ioctl_enotty(void)
{
	struct file file = {
		.f_mode = FMODE_READ | FMODE_WRITE,
	};

	TEST_BEGIN("syscall compat: default ioctl enotty");
	{
		TEST_ASSERT_EQ(vfs_ioctl(NULL, 0x5401, 0), -EINVAL);
		TEST_ASSERT_EQ(vfs_ioctl(&file, 0x5401, 0), -ENOTTY);
		TEST_ASSERT_EQ(vfs_ioctl(&file, 0xdeadbeef, 0), -ENOTTY);
	}
	TEST_END("syscall compat: default ioctl enotty");
	return;
fail:
	TEST_FAIL("syscall compat: default ioctl enotty", "see above");
}

void test_console_tty_line_discipline(void)
{
	struct termios termios = {
		.c_iflag = ICRNL,
		.c_oflag = OPOST | ONLCR,
		.c_lflag = ISIG | ICANON | ECHO,
	};
	char out[16];
	char echo[32];
	int signal = 0;

	termios.c_cc[VINTR] = 3;
	termios.c_cc[VERASE] = 127;
	termios.c_cc[VEOF] = 4;
	termios.c_cc[VSUSP] = 26;

	TEST_BEGIN("syscall compat: console tty line discipline");
	{
		TEST_ASSERT_EQ(console_tty_write_for_test(&termios, "a\nb", 3,
							  out, sizeof(out)),
			       4);
		TEST_ASSERT_EQ(out[0], 'a');
		TEST_ASSERT_EQ(out[1], '\r');
		TEST_ASSERT_EQ(out[2], '\n');
		TEST_ASSERT_EQ(out[3], 'b');

		TEST_ASSERT_EQ(console_tty_read_stream_for_test(
				       &termios,
				       "ab\x7f"
				       "cd\n",
				       6, out, sizeof(out), echo, sizeof(echo),
				       &signal),
			       4);
		TEST_ASSERT_EQ(signal, 0);
		TEST_ASSERT_EQ(out[0], 'a');
		TEST_ASSERT_EQ(out[1], 'c');
		TEST_ASSERT_EQ(out[2], 'd');
		TEST_ASSERT_EQ(out[3], '\n');
		TEST_ASSERT_EQ(echo[0], 'a');
		TEST_ASSERT_EQ(echo[1], 'b');
		TEST_ASSERT_EQ(echo[2], '\b');
		TEST_ASSERT_EQ(echo[3], ' ');
		TEST_ASSERT_EQ(echo[4], '\b');

		TEST_ASSERT_EQ(console_tty_read_stream_for_test(
				       &termios, "\004", 1, out, sizeof(out),
				       echo, sizeof(echo), &signal),
			       0);
		TEST_ASSERT_EQ(signal, 0);

		TEST_ASSERT_EQ(console_tty_read_stream_for_test(
				       &termios, "\003", 1, out, sizeof(out),
				       echo, sizeof(echo), &signal),
			       -EINTR);
		TEST_ASSERT_EQ(signal, 2);

		TEST_ASSERT_EQ(console_tty_read_stream_for_test(
				       &termios, "\032", 1, out, sizeof(out),
				       echo, sizeof(echo), &signal),
			       1);
		TEST_ASSERT_EQ(signal, 0);
		TEST_ASSERT_EQ(out[0], 26);
	}
	TEST_END("syscall compat: console tty line discipline");
	return;
fail:
	TEST_FAIL("syscall compat: console tty line discipline", "see above");
}

void test_tty_signal_delivery_policy(void)
{
	struct task_struct *saved = current;
	struct task_struct *task = NULL;

	TEST_BEGIN("syscall compat: tty signal delivery");
	{
		TEST_ASSERT_EQ(tty_deliver_signal(NSIG), -EINVAL);

		task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);
		TEST_ASSERT_EQ(task_init_resources(task), 0);
		current = task;

		TEST_ASSERT_EQ(tty_deliver_signal(SIGINT), 0);
		TEST_ASSERT_EQ(task->signal->shared_pending,
			       signal_mask(SIGINT));
		TEST_ASSERT_EQ(task->pending, (uint64_t)0);
	}
	TEST_END("syscall compat: tty signal delivery");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: tty signal delivery", "see above");
cleanup:
	current = saved;
	if (task)
		task_free(task);
}

void test_signal_rt_sigsetsize_validation(void)
{
	struct trap_frame tf = {0};

	TEST_BEGIN("syscall compat: rt signal sigsetsize validation");
	{
		tf.a0 = SIGUSR1;
		tf.a1 = 0;
		tf.a2 = 0;
		tf.a3 = 0;
		TEST_ASSERT_EQ(sys_sigaction(&tf), -EINVAL);

		tf.a3 = sizeof(unsigned long) + 1;
		TEST_ASSERT_EQ(sys_sigaction(&tf), -EINVAL);

		tf.a3 = sizeof(unsigned long);
		TEST_ASSERT_EQ(sys_sigaction(&tf), 0);

		memset(&tf, 0, sizeof(tf));
		tf.a0 = SIG_BLOCK;
		tf.a1 = 0;
		tf.a2 = 0;
		tf.a3 = 0;
		TEST_ASSERT_EQ(sys_sigprocmask(&tf), -EINVAL);

		tf.a3 = sizeof(unsigned long) + 1;
		TEST_ASSERT_EQ(sys_sigprocmask(&tf), -EINVAL);

		tf.a3 = sizeof(unsigned long);
		TEST_ASSERT_EQ(sys_sigprocmask(&tf), 0);
	}
	TEST_END("syscall compat: rt signal sigsetsize validation");
	return;
fail:
	TEST_FAIL("syscall compat: rt signal sigsetsize validation",
		  "see above");
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

void test_pipe2_file_alloc_failure_cleanup(void)
{
	uint32_t live_before;
	uint32_t live_after;
	int fds[2] = {-1, -1};

	TEST_BEGIN("syscall compat: pipe2 allocation failure cleanup");
	{
		live_before = pipe_test_live_buffers();
		pipe_test_set_file_alloc_fail_at(2);
		TEST_ASSERT_EQ(do_pipe2(fds, 0), -ENOMEM);
		pipe_test_set_file_alloc_fail_at(0);
		live_after = pipe_test_live_buffers();

		TEST_ASSERT_EQ(live_after, live_before);
		TEST_ASSERT_EQ(fds[0], -1);
		TEST_ASSERT_EQ(fds[1], -1);
	}
	TEST_END("syscall compat: pipe2 allocation failure cleanup");
	return;
fail:
	pipe_test_set_file_alloc_fail_at(0);
	TEST_FAIL("syscall compat: pipe2 allocation failure cleanup",
		  "see above");
}

#include <kernel/fs.h>
#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <kernel/pipe.h>
#include <kernel/resource.h>
#include <kernel/signal.h>
#include <kernel/statfs.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/test.h>
#include <kernel/session.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>
#include <uapi/futex.h>
#include <uapi/poll.h>
#include <uapi/resource.h>
#include <uapi/reboot.h>
#include <uapi/time.h>
#include <uapi/tty.h>

ssize_t tty_console_read_stream_for_test(const struct termios *termios,
					 const char *input, size_t input_len,
					 char *out, size_t out_size, char *echo,
					 size_t echo_size, int *signal);
ssize_t tty_console_write_for_test(const struct termios *termios,
				   const char *input, size_t input_len,
				   char *out, size_t out_size);

static struct wait_session *poll_test_session;

static void test_release_published_task(struct task_struct *task)
{
	if (!task)
		return;
	session_process_abort(task);
	task_unpublish(task);
	task_put(task);
}

static void test_console_release(void)
{
	int ret = session_console_release();

	(void)ret;
}

static int poll_test_error(struct file *file, uint32_t events,
			   struct wait_session *session)
{
	(void)file;
	(void)events;
	poll_test_session = session;
	return -ENOMEM;
}

int test_rlimit_defaults(void)
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
	return __test_ret;
fail:
	TEST_FAIL("syscall compat: rlimit defaults", "see above");

	return __test_ret;
}

int test_vfs_default_poll_masks(void)
{
	struct file file = {
		.f_mode = FMODE_READ | FMODE_WRITE,
	};

	TEST_BEGIN("syscall compat: default poll masks");
	{
		TEST_ASSERT_EQ(vfs_poll(&file, POLLIN, NULL), (uint32_t)POLLIN);
		TEST_ASSERT_EQ(vfs_poll(&file, POLLOUT, NULL),
			       (uint32_t)POLLOUT);
		TEST_ASSERT_EQ(vfs_poll(&file, POLLIN | POLLOUT, NULL),
			       (uint32_t)(POLLIN | POLLOUT));
		TEST_ASSERT_EQ(vfs_poll(NULL, POLLIN, NULL),
			       (uint32_t)POLLNVAL);
	}
	TEST_END("syscall compat: default poll masks");
	return __test_ret;
fail:
	TEST_FAIL("syscall compat: default poll masks", "see above");

	return __test_ret;
}

int test_vfs_poll_propagates_session_errors(void)
{
	static const struct file_operations fops = {
		.poll = poll_test_error,
	};
	struct wait_session *session = (struct wait_session *)1;
	struct file file = {
		.f_op = &fops,
	};

	TEST_BEGIN("syscall compat: poll wait-context propagation");
	{
		poll_test_session = NULL;
		TEST_ASSERT_EQ(vfs_poll(&file, POLLIN, session), -ENOMEM);
		TEST_ASSERT_EQ(poll_test_session, session);
	}
	TEST_END("syscall compat: poll wait-context propagation");
	return __test_ret;
fail:
	TEST_FAIL("syscall compat: poll wait-context propagation", "see above");

	return __test_ret;
}

int test_vfs_default_ioctl_enotty(void)
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
	return __test_ret;
fail:
	TEST_FAIL("syscall compat: default ioctl enotty", "see above");

	return __test_ret;
}

int test_console_tty_line_discipline(void)
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
		TEST_ASSERT_EQ(tty_console_write_for_test(&termios, "a\nb", 3,
							  out, sizeof(out)),
			       4);
		TEST_ASSERT_EQ(out[0], 'a');
		TEST_ASSERT_EQ(out[1], '\r');
		TEST_ASSERT_EQ(out[2], '\n');
		TEST_ASSERT_EQ(out[3], 'b');

		TEST_ASSERT_EQ(tty_console_read_stream_for_test(
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

		TEST_ASSERT_EQ(tty_console_read_stream_for_test(
				       &termios, "\004", 1, out, sizeof(out),
				       echo, sizeof(echo), &signal),
			       0);
		TEST_ASSERT_EQ(signal, 0);

		TEST_ASSERT_EQ(tty_console_read_stream_for_test(
				       &termios, "\003", 1, out, sizeof(out),
				       echo, sizeof(echo), &signal),
			       -EINTR);
		TEST_ASSERT_EQ(signal, 2);

		TEST_ASSERT_EQ(tty_console_read_stream_for_test(
				       &termios, "\032", 1, out, sizeof(out),
				       echo, sizeof(echo), &signal),
			       -EINTR);
		TEST_ASSERT_EQ(signal, SIGTSTP);
	}
	TEST_END("syscall compat: console tty line discipline");
	return __test_ret;
fail:
	TEST_FAIL("syscall compat: console tty line discipline", "see above");

	return __test_ret;
}

int test_tty_signal_delivery_policy(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *task = NULL;

	TEST_BEGIN("syscall compat: tty signal delivery");
	{
		TEST_ASSERT_EQ(session_console_deliver_foreground_signal(NSIG),
			       -EINVAL);

		task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);
		TEST_ASSERT_EQ(task_init_resources(task), 0);
		task_publish(task);
		set_current_task(task);

		TEST_ASSERT_EQ(
			session_console_deliver_foreground_signal(SIGINT),
			-ESRCH);
		TEST_ASSERT_EQ(task->resources.signal->shared_pending,
			       (uint64_t)0);
		TEST_ASSERT_EQ(task->sigctx.pending, (uint64_t)0);
	}
	TEST_END("syscall compat: tty signal delivery");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: tty signal delivery", "see above");
cleanup:
	set_current_task(saved);
	if (task)
		test_release_published_task(task);

	return __test_ret;
}

int test_tty_console_job_control_policy(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *task = NULL;
	pid_t pgid = -1;
	pid_t sid = -1;

	TEST_BEGIN("syscall compat: tty console job control");
	{
		TEST_ASSERT_EQ(session_console_get_foreground_pgid(&pgid),
			       -ENOTTY);

		task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);
		TEST_ASSERT_EQ(task_init_resources(task), 0);
		task_publish(task);
		set_current_task(task);

		TEST_ASSERT_EQ(session_console_get_foreground_pgid(NULL),
			       -EINVAL);
		TEST_ASSERT_EQ(session_console_get_sid(NULL), -EINVAL);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		TEST_ASSERT_EQ(session_console_get_foreground_pgid(&pgid), 0);
		TEST_ASSERT_EQ(pgid, task_test_pgid(task));
		TEST_ASSERT_EQ(session_console_get_sid(&sid), 0);
		TEST_ASSERT_EQ(sid, task_test_sid(task));

		TEST_ASSERT_EQ(session_console_set_foreground_pgid(-1),
			       -EINVAL);
		TEST_ASSERT_EQ(
			session_console_set_foreground_pgid(task_pid(task) + 1),
			-EPERM);
		TEST_ASSERT_EQ(session_console_set_foreground_pgid(
				       task_test_pgid(task)),
			       0);
		TEST_ASSERT_EQ(
			session_console_deliver_foreground_signal(SIGINT), 0);
		TEST_ASSERT_EQ(task->resources.signal->shared_pending &
				       signal_mask(SIGINT),
			       signal_mask(SIGINT));

		TEST_ASSERT_EQ(session_console_release(), 0);
		TEST_ASSERT_EQ(session_console_get_foreground_pgid(&pgid),
			       -ENOTTY);
	}
	TEST_END("syscall compat: tty console job control");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: tty console job control", "see above");
cleanup:
	if (current_task() == task)
		test_console_release();
	set_current_task(saved);
	if (task)
		test_release_published_task(task);

	return __test_ret;
}

int test_tty_controlling_terminal_is_explicit(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *owner = NULL;
	struct task_struct *peer = NULL;
	pid_t sid = -1;

	TEST_BEGIN("syscall compat: controlling tty association is explicit");
	{
		owner = task_alloc();
		TEST_ASSERT_NOT_NULL(owner);
		TEST_ASSERT_EQ(task_init_resources(owner), 0);
		set_current_task(owner);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		peer = task_alloc();
		TEST_ASSERT_NOT_NULL(peer);
		TEST_ASSERT_EQ(task_init_resources(peer), 0);
		task_test_set_process_identity(peer, task_test_pgid(peer),
					       task_test_sid(owner));
		set_current_task(peer);

		TEST_ASSERT_EQ(session_console_get_sid(&sid), -ENOTTY);
	}
	TEST_END("syscall compat: controlling tty association is explicit");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: controlling tty association is explicit",
		  "see above");
cleanup:
	if (owner) {
		set_current_task(owner);
		test_console_release();
	}
	set_current_task(saved);
	if (peer)
		task_free(peer);
	if (owner)
		task_free(owner);

	return __test_ret;
}

int test_tty_fork_inherits_controlling_terminal(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *parent = NULL;
	struct task_struct *child = NULL;
	pid_t sid = -1;

	TEST_BEGIN("syscall compat: fork inherits controlling tty");
	{
		parent = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_EQ(task_init_resources(parent), 0);
		task_publish(parent);
		set_current_task(parent);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		child = task_alloc();
		TEST_ASSERT_NOT_NULL(child);
		TEST_ASSERT_EQ(task_init_resources(child), 0);
		TEST_ASSERT_EQ(signals_clone(child, false, false, false), 0);
		TEST_ASSERT_EQ(
			session_process_clone_prepare(child, parent, false), 0);
		task_publish(child);
		task_test_set_process_identity(child, task_test_pgid(parent),
					       task_test_sid(parent));
		set_current_task(child);

		TEST_ASSERT_EQ(session_console_get_sid(&sid), 0);
		TEST_ASSERT_EQ(sid, task_test_sid(parent));
	}
	TEST_END("syscall compat: fork inherits controlling tty");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: fork inherits controlling tty", "see above");
cleanup:
	if (child) {
		set_current_task(child);
		test_console_release();
	} else if (parent) {
		set_current_task(parent);
		test_console_release();
	}
	set_current_task(saved);
	if (child)
		test_release_published_task(child);
	if (parent)
		test_release_published_task(parent);

	return __test_ret;
}

int test_tty_clone_release_drops_attachment(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *parent = NULL;
	struct task_struct *child = NULL;

	TEST_BEGIN("syscall compat: tty clone release drops attachment");
	{
		parent = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_EQ(task_init_resources(parent), 0);
		task_publish(parent);
		set_current_task(parent);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		child = task_alloc();
		TEST_ASSERT_NOT_NULL(child);
		TEST_ASSERT_EQ(task_init_resources(child), 0);
		TEST_ASSERT_EQ(signals_clone(child, false, false, false), 0);
		TEST_ASSERT_EQ(
			session_process_clone_prepare(child, parent, false), 0);
		task_publish(child);
		task_test_set_process_identity(child, task_pid(child),
					       task_test_sid(parent));
		TEST_ASSERT_EQ(session_console_set_foreground_pgid(
				       task_test_pgid(child)),
			       0);

		test_release_published_task(child);
		child = NULL;
		TEST_ASSERT_EQ(
			session_console_deliver_foreground_signal(SIGINT),
			-ESRCH);
	}
	TEST_END("syscall compat: tty clone release drops attachment");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: tty clone release drops attachment",
		  "see above");
cleanup:
	if (parent) {
		set_current_task(parent);
		test_console_release();
	}
	set_current_task(saved);
	if (child)
		test_release_published_task(child);
	if (parent)
		test_release_published_task(parent);

	return __test_ret;
}

int test_tty_process_exit_detaches_own_attachment(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *parent = NULL;
	struct task_struct *child = NULL;
	pid_t sid = -1;

	TEST_BEGIN("syscall compat: tty process exit detaches own attachment");
	{
		parent = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_EQ(task_init_resources(parent), 0);
		set_current_task(parent);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		child = task_alloc();
		TEST_ASSERT_NOT_NULL(child);
		TEST_ASSERT_EQ(task_init_resources(child), 0);
		TEST_ASSERT_EQ(signals_clone(child, false, false, false), 0);
		TEST_ASSERT_EQ(
			session_process_clone_prepare(child, parent, false), 0);
		task_test_set_process_identity(child, task_test_pgid(parent),
					       task_test_sid(parent));

		set_current_task(child);
		session_process_exit(child);
		TEST_ASSERT_EQ(session_console_get_sid(&sid), -ENOTTY);
		set_current_task(parent);
		TEST_ASSERT_EQ(session_console_get_sid(&sid), 0);
		TEST_ASSERT_EQ(sid, task_test_sid(parent));
	}
	TEST_END("syscall compat: tty process exit detaches own attachment");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: tty process exit detaches own attachment",
		  "see above");
cleanup:
	if (parent) {
		set_current_task(parent);
		test_console_release();
	}
	set_current_task(saved);
	if (child)
		task_free(child);
	if (parent)
		task_free(parent);

	return __test_ret;
}

int test_tty_setsid_detaches_controlling_terminal(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *parent = NULL;
	struct task_struct *child = NULL;
	struct trap_frame tf = {0};
	pid_t sid = -1;

	TEST_BEGIN("syscall compat: setsid detaches controlling tty");
	{
		parent = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_EQ(task_init_resources(parent), 0);
		set_current_task(parent);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		child = task_alloc();
		TEST_ASSERT_NOT_NULL(child);
		TEST_ASSERT_EQ(task_init_resources(child), 0);
		TEST_ASSERT_EQ(signals_clone(child, false, false, false), 0);
		TEST_ASSERT_EQ(
			session_process_clone_prepare(child, parent, false), 0);
		task_test_set_process_identity(child, task_test_pgid(parent),
					       task_test_sid(parent));
		set_current_task(child);

		TEST_ASSERT_EQ(sys_setsid(&tf), task_pid(child));
		TEST_ASSERT_EQ(session_console_get_sid(&sid), -ENOTTY);
	}
	TEST_END("syscall compat: setsid detaches controlling tty");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: setsid detaches controlling tty",
		  "see above");
cleanup:
	if (parent) {
		set_current_task(parent);
		test_console_release();
	}
	set_current_task(saved);
	if (child)
		task_free(child);
	if (parent)
		task_free(parent);

	return __test_ret;
}

int test_tty_nonleader_detaches_only_itself(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *leader = NULL;
	struct task_struct *member = NULL;
	pid_t sid = -1;

	TEST_BEGIN("syscall compat: nonleader detaches only itself");
	{
		leader = task_alloc();
		TEST_ASSERT_NOT_NULL(leader);
		TEST_ASSERT_EQ(task_init_resources(leader), 0);
		task_publish(leader);
		set_current_task(leader);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		member = task_alloc();
		TEST_ASSERT_NOT_NULL(member);
		TEST_ASSERT_EQ(task_init_resources(member), 0);
		TEST_ASSERT_EQ(signals_clone(member, false, false, false), 0);
		TEST_ASSERT_EQ(
			session_process_clone_prepare(member, leader, false),
			0);
		task_test_set_process_identity(member, task_test_pgid(leader),
					       task_test_sid(leader));
		set_current_task(member);

		TEST_ASSERT_EQ(session_console_release(), 0);
		TEST_ASSERT_EQ(session_console_get_sid(&sid), -ENOTTY);
		set_current_task(leader);
		TEST_ASSERT_EQ(session_console_get_sid(&sid), 0);
		TEST_ASSERT_EQ(sid, task_test_sid(leader));
	}
	TEST_END("syscall compat: nonleader detaches only itself");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: nonleader detaches only itself",
		  "see above");
cleanup:
	if (leader) {
		set_current_task(leader);
		test_console_release();
	}
	set_current_task(saved);
	if (member)
		test_release_published_task(member);
	if (leader)
		test_release_published_task(leader);

	return __test_ret;
}

int test_tty_leader_detaches_entire_session(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *leader = NULL;
	struct task_struct *member = NULL;
	struct task_struct *next_owner = NULL;
	pid_t sid = -1;

	TEST_BEGIN("syscall compat: leader detaches entire tty session");
	{
		leader = task_alloc();
		TEST_ASSERT_NOT_NULL(leader);
		TEST_ASSERT_EQ(task_init_resources(leader), 0);
		task_publish(leader);
		set_current_task(leader);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		member = task_alloc();
		TEST_ASSERT_NOT_NULL(member);
		TEST_ASSERT_EQ(task_init_resources(member), 0);
		TEST_ASSERT_EQ(signals_clone(member, false, false, false), 0);
		TEST_ASSERT_EQ(
			session_process_clone_prepare(member, leader, false),
			0);
		task_test_set_process_identity(member, task_test_pgid(leader),
					       task_test_sid(leader));

		set_current_task(leader);
		TEST_ASSERT_EQ(session_console_release(), 0);

		next_owner = task_alloc();
		TEST_ASSERT_NOT_NULL(next_owner);
		TEST_ASSERT_EQ(task_init_resources(next_owner), 0);
		set_current_task(next_owner);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		set_current_task(member);
		TEST_ASSERT_EQ(session_console_get_sid(&sid), -ENOTTY);
	}
	TEST_END("syscall compat: leader detaches entire tty session");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: leader detaches entire tty session",
		  "see above");
cleanup:
	if (next_owner) {
		set_current_task(next_owner);
		test_console_release();
	} else if (leader) {
		set_current_task(leader);
		test_console_release();
	}
	set_current_task(saved);
	if (next_owner)
		test_release_published_task(next_owner);
	if (member)
		test_release_published_task(member);
	if (leader)
		test_release_published_task(leader);

	return __test_ret;
}

int test_tty_leader_release_signals_foreground_pgrp(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *leader = NULL;
	struct task_struct *foreground = NULL;
	struct timespec timeout = {0};
	siginfo_t info = {0};

	TEST_BEGIN(
		"syscall compat: tty leader release signals foreground pgrp");
	{
		leader = task_alloc();
		TEST_ASSERT_NOT_NULL(leader);
		TEST_ASSERT_EQ(task_init_resources(leader), 0);
		set_current_task(leader);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		foreground = task_alloc();
		TEST_ASSERT_NOT_NULL(foreground);
		TEST_ASSERT_EQ(task_init_resources(foreground), 0);
		TEST_ASSERT_EQ(signals_clone(foreground, false, false, false),
			       0);
		TEST_ASSERT_EQ(session_process_clone_prepare(foreground, leader,
							     false),
			       0);
		task_publish(foreground);
		task_test_set_process_identity(foreground, task_pid(foreground),
					       task_test_sid(leader));
		TEST_ASSERT_EQ(session_console_set_foreground_pgid(
				       task_test_pgid(foreground)),
			       0);

		TEST_ASSERT_EQ(session_console_release(), 0);
		set_current_task(foreground);
		TEST_ASSERT_EQ(signal_wait_pending(signal_mask(SIGHUP) |
							   signal_mask(SIGCONT),
						   &timeout, &info),
			       SIGHUP);
		TEST_ASSERT_EQ(signal_wait_pending(signal_mask(SIGCONT),
						   &timeout, &info),
			       SIGCONT);
	}
	TEST_END("syscall compat: tty leader release signals foreground pgrp");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: tty leader release signals foreground pgrp",
		  "see above");
cleanup:
	if (leader) {
		set_current_task(leader);
		test_console_release();
	}
	set_current_task(saved);
	if (foreground)
		test_release_published_task(foreground);
	if (leader)
		test_release_published_task(leader);

	return __test_ret;
}

int test_tty_session_leader_exit_releases_console(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *leader = NULL;
	struct task_struct *member = NULL;
	struct task_struct *next_owner = NULL;
	pid_t sid = -1;

	TEST_BEGIN("syscall compat: tty session leader exit releases console");
	{
		leader = task_alloc();
		TEST_ASSERT_NOT_NULL(leader);
		TEST_ASSERT_EQ(task_init_resources(leader), 0);
		set_current_task(leader);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		member = task_alloc();
		TEST_ASSERT_NOT_NULL(member);
		TEST_ASSERT_EQ(task_init_resources(member), 0);
		TEST_ASSERT_EQ(signals_clone(member, false, false, false), 0);
		TEST_ASSERT_EQ(
			session_process_clone_prepare(member, leader, false),
			0);
		task_test_set_process_identity(member, task_test_pgid(leader),
					       task_test_sid(leader));

		session_process_exit(leader);
		next_owner = task_alloc();
		TEST_ASSERT_NOT_NULL(next_owner);
		TEST_ASSERT_EQ(task_init_resources(next_owner), 0);
		set_current_task(next_owner);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		set_current_task(member);
		TEST_ASSERT_EQ(session_console_get_sid(&sid), -ENOTTY);
	}
	TEST_END("syscall compat: tty session leader exit releases console");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: tty session leader exit releases console",
		  "see above");
cleanup:
	if (next_owner) {
		set_current_task(next_owner);
		test_console_release();
	} else if (leader) {
		set_current_task(leader);
		test_console_release();
	}
	set_current_task(saved);
	if (next_owner)
		task_free(next_owner);
	if (member)
		task_free(member);
	if (leader)
		task_free(leader);

	return __test_ret;
}

int test_tty_root_force_steals_console(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *old_owner = NULL;
	struct task_struct *old_member = NULL;
	struct task_struct *new_owner = NULL;
	pid_t sid = -1;

	TEST_BEGIN("syscall compat: root TIOCSCTTY force steals console");
	{
		old_owner = task_alloc();
		TEST_ASSERT_NOT_NULL(old_owner);
		TEST_ASSERT_EQ(task_init_resources(old_owner), 0);
		set_current_task(old_owner);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		old_member = task_alloc();
		TEST_ASSERT_NOT_NULL(old_member);
		TEST_ASSERT_EQ(task_init_resources(old_member), 0);
		TEST_ASSERT_EQ(signals_clone(old_member, false, false, false),
			       0);
		TEST_ASSERT_EQ(session_process_clone_prepare(old_member,
							     old_owner, false),
			       0);
		task_test_set_process_identity(old_member,
					       task_test_pgid(old_owner),
					       task_test_sid(old_owner));

		new_owner = task_alloc();
		TEST_ASSERT_NOT_NULL(new_owner);
		TEST_ASSERT_EQ(task_init_resources(new_owner), 0);
		set_current_task(new_owner);
		TEST_ASSERT_EQ(task_uid(new_owner), (uid_t)0);
		TEST_ASSERT_EQ(session_console_acquire(1), 0);
		TEST_ASSERT_EQ(session_console_get_sid(&sid), 0);
		TEST_ASSERT_EQ(sid, task_test_sid(new_owner));

		set_current_task(old_member);
		TEST_ASSERT_EQ(session_console_get_sid(&sid), -ENOTTY);
	}
	TEST_END("syscall compat: root TIOCSCTTY force steals console");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: root TIOCSCTTY force steals console",
		  "see above");
cleanup:
	if (new_owner) {
		set_current_task(new_owner);
		test_console_release();
	} else if (old_owner) {
		set_current_task(old_owner);
		test_console_release();
	}
	set_current_task(saved);
	if (new_owner)
		task_free(new_owner);
	if (old_member)
		task_free(old_member);
	if (old_owner)
		task_free(old_owner);

	return __test_ret;
}

int test_tty_force_steal_signals_old_foreground_pgrp(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *old_owner = NULL;
	struct task_struct *old_foreground = NULL;
	struct task_struct *new_owner = NULL;
	struct timespec timeout = {0};
	siginfo_t info = {0};

	TEST_BEGIN("syscall compat: force steal hangs up old foreground pgrp");
	{
		old_owner = task_alloc();
		TEST_ASSERT_NOT_NULL(old_owner);
		TEST_ASSERT_EQ(task_init_resources(old_owner), 0);
		task_publish(old_owner);
		set_current_task(old_owner);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		old_foreground = task_alloc();
		TEST_ASSERT_NOT_NULL(old_foreground);
		TEST_ASSERT_EQ(task_init_resources(old_foreground), 0);
		TEST_ASSERT_EQ(
			signals_clone(old_foreground, false, false, false), 0);
		TEST_ASSERT_EQ(session_process_clone_prepare(old_foreground,
							     old_owner, false),
			       0);
		task_publish(old_foreground);
		task_test_set_process_identity(old_foreground,
					       task_pid(old_foreground),
					       task_test_sid(old_owner));
		TEST_ASSERT_EQ(session_console_set_foreground_pgid(
				       task_test_pgid(old_foreground)),
			       0);

		new_owner = task_alloc();
		TEST_ASSERT_NOT_NULL(new_owner);
		TEST_ASSERT_EQ(task_init_resources(new_owner), 0);
		set_current_task(new_owner);
		TEST_ASSERT_EQ(session_console_acquire(1), 0);

		set_current_task(old_foreground);
		TEST_ASSERT_EQ(signal_wait_pending(signal_mask(SIGHUP) |
							   signal_mask(SIGCONT),
						   &timeout, &info),
			       SIGHUP);
		TEST_ASSERT_EQ(signal_wait_pending(signal_mask(SIGCONT),
						   &timeout, &info),
			       SIGCONT);
	}
	TEST_END("syscall compat: force steal hangs up old foreground pgrp");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: force steal hangs up old foreground pgrp",
		  "see above");
cleanup:
	if (new_owner) {
		set_current_task(new_owner);
		test_console_release();
	} else if (old_owner) {
		set_current_task(old_owner);
		test_console_release();
	}
	set_current_task(saved);
	if (new_owner)
		task_free(new_owner);
	if (old_foreground)
		test_release_published_task(old_foreground);
	if (old_owner)
		test_release_published_task(old_owner);

	return __test_ret;
}

int test_tty_console_steal_requires_root_force(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *owner = NULL;
	struct task_struct *challenger = NULL;
	pid_t sid = -1;

	TEST_BEGIN("syscall compat: TIOCSCTTY steal requires root force");
	{
		owner = task_alloc();
		TEST_ASSERT_NOT_NULL(owner);
		TEST_ASSERT_EQ(task_init_resources(owner), 0);
		task_publish(owner);
		set_current_task(owner);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		challenger = task_alloc();
		TEST_ASSERT_NOT_NULL(challenger);
		TEST_ASSERT_EQ(task_init_resources(challenger), 0);
		set_current_task(challenger);
		TEST_ASSERT_EQ(session_console_acquire(0), -EPERM);
		task_set_uid(challenger, 1000);
		TEST_ASSERT_EQ(session_console_acquire(1), -EPERM);

		set_current_task(owner);
		TEST_ASSERT_EQ(session_console_get_sid(&sid), 0);
		TEST_ASSERT_EQ(sid, task_test_sid(owner));
	}
	TEST_END("syscall compat: TIOCSCTTY steal requires root force");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: TIOCSCTTY steal requires root force",
		  "see above");
cleanup:
	if (owner) {
		set_current_task(owner);
		test_console_release();
	}
	set_current_task(saved);
	if (challenger)
		test_release_published_task(challenger);
	if (owner)
		test_release_published_task(owner);

	return __test_ret;
}

int test_tty_stale_foreground_pgrp_gets_no_signal(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *owner = NULL;
	struct task_struct *old_foreground = NULL;

	TEST_BEGIN("syscall compat: stale tty foreground pgrp gets no signal");
	{
		owner = task_alloc();
		TEST_ASSERT_NOT_NULL(owner);
		TEST_ASSERT_EQ(task_init_resources(owner), 0);
		task_publish(owner);
		set_current_task(owner);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		old_foreground = task_alloc();
		TEST_ASSERT_NOT_NULL(old_foreground);
		TEST_ASSERT_EQ(task_init_resources(old_foreground), 0);
		TEST_ASSERT_EQ(
			signals_clone(old_foreground, false, false, false), 0);
		TEST_ASSERT_EQ(session_process_clone_prepare(old_foreground,
							     owner, false),
			       0);
		task_publish(old_foreground);
		task_test_set_process_identity(old_foreground,
					       task_pid(old_foreground),
					       task_test_sid(owner));
		TEST_ASSERT_EQ(session_console_set_foreground_pgid(
				       task_test_pgid(old_foreground)),
			       0);

		task_test_set_process_identity(old_foreground,
					       task_test_pgid(old_foreground),
					       task_pid(old_foreground));
		TEST_ASSERT_EQ(
			session_console_deliver_foreground_signal(SIGINT),
			-ESRCH);
		TEST_ASSERT_EQ(old_foreground->resources.signal->shared_pending,
			       (uint64_t)0);
	}
	TEST_END("syscall compat: stale tty foreground pgrp gets no signal");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: stale tty foreground pgrp gets no signal",
		  "see above");
cleanup:
	if (owner) {
		set_current_task(owner);
		test_console_release();
	}
	set_current_task(saved);
	if (old_foreground)
		test_release_published_task(old_foreground);
	if (owner)
		test_release_published_task(owner);

	return __test_ret;
}

int test_tty_detach_does_not_signal_reused_foreground_pgrp(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *owner = NULL;
	struct task_struct *old_foreground = NULL;

	TEST_BEGIN("syscall compat: tty detach ignores reused foreground pgrp");
	{
		owner = task_alloc();
		TEST_ASSERT_NOT_NULL(owner);
		TEST_ASSERT_EQ(task_init_resources(owner), 0);
		task_publish(owner);
		set_current_task(owner);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		old_foreground = task_alloc();
		TEST_ASSERT_NOT_NULL(old_foreground);
		TEST_ASSERT_EQ(task_init_resources(old_foreground), 0);
		TEST_ASSERT_EQ(
			signals_clone(old_foreground, false, false, false), 0);
		TEST_ASSERT_EQ(session_process_clone_prepare(old_foreground,
							     owner, false),
			       0);
		task_publish(old_foreground);
		task_test_set_process_identity(old_foreground,
					       task_pid(old_foreground),
					       task_test_sid(owner));
		TEST_ASSERT_EQ(session_console_set_foreground_pgid(
				       task_test_pgid(old_foreground)),
			       0);

		task_test_set_process_identity(old_foreground,
					       task_test_pgid(old_foreground),
					       task_pid(old_foreground));
		TEST_ASSERT_EQ(session_console_release(), 0);
		TEST_ASSERT_EQ(old_foreground->resources.signal->shared_pending,
			       (uint64_t)0);
	}
	TEST_END("syscall compat: tty detach ignores reused foreground pgrp");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: tty detach ignores reused foreground pgrp",
		  "see above");
cleanup:
	if (owner) {
		set_current_task(owner);
		test_console_release();
	}
	set_current_task(saved);
	if (old_foreground)
		test_release_published_task(old_foreground);
	if (owner)
		test_release_published_task(owner);

	return __test_ret;
}

int test_tty_empty_foreground_pgrp_is_cleared(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *owner = NULL;
	struct task_struct *child = NULL;
	pid_t foreground_pgid = -1;

	TEST_BEGIN("syscall compat: empty foreground pgrp is cleared");
	{
		owner = task_alloc();
		TEST_ASSERT_NOT_NULL(owner);
		TEST_ASSERT_EQ(task_init_resources(owner), 0);
		task_publish(owner);
		set_current_task(owner);
		TEST_ASSERT_EQ(session_console_acquire(0), 0);

		child = task_alloc();
		TEST_ASSERT_NOT_NULL(child);
		TEST_ASSERT_EQ(task_init_resources(child), 0);
		TEST_ASSERT_EQ(signals_clone(child, false, false, false), 0);
		TEST_ASSERT_EQ(
			session_process_clone_prepare(child, owner, false), 0);
		child->links.parent = owner;
		task_publish(child);

		TEST_ASSERT_EQ(session_process_setpgid(task_pid(child), 0), 0);
		TEST_ASSERT_EQ(
			session_console_set_foreground_pgid(task_pid(child)),
			0);
		TEST_ASSERT_EQ(session_process_setpgid(task_pid(child),
						       task_test_pgid(owner)),
			       0);
		TEST_ASSERT_EQ(
			session_console_get_foreground_pgid(&foreground_pgid),
			0);
		TEST_ASSERT_EQ(foreground_pgid, (pid_t)0);
		TEST_ASSERT_EQ(
			session_console_deliver_foreground_signal(SIGINT),
			-ESRCH);
	}
	TEST_END("syscall compat: empty foreground pgrp is cleared");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: empty foreground pgrp is cleared",
		  "see above");
cleanup:
	if (owner) {
		set_current_task(owner);
		test_console_release();
	}
	set_current_task(saved);
	if (child)
		test_release_published_task(child);
	if (owner)
		test_release_published_task(owner);

	return __test_ret;
}

int test_signal_rt_sigsetsize_validation(void)
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
	return __test_ret;
fail:
	TEST_FAIL("syscall compat: rt signal sigsetsize validation",
		  "see above");

	return __test_ret;
}

int test_init_signal_protection(void)
{
	struct task_struct *saved_current = current_task();
	struct task_struct *saved_init = init_task;
	struct task_struct *task = NULL;
	struct trap_frame tf = {0};
	struct timespec zero = {0};
	siginfo_t info;
	uint64_t term_mask = signal_mask(SIGTERM);
	uint64_t stop_mask = signal_mask(SIGSTOP);
	uint64_t user_mask = signal_mask(SIGUSR1);

	TEST_BEGIN("syscall compat: init signal protection");
	{
		task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);
		TEST_ASSERT_EQ(task_init_resources(task), 0);
		set_current_task(task);
		init_task = task;

		signal_block_mask(task, term_mask);
		TEST_ASSERT_EQ(send_group_signal(SIGTERM, task), 0);
		TEST_ASSERT_EQ(task_signal_state(task)->shared_pending &
				       term_mask,
			       term_mask);
		TEST_ASSERT_EQ(signal_wait_pending(term_mask, &zero, &info),
			       SIGTERM);
		TEST_ASSERT_EQ(info.si_signo, SIGTERM);
		signal_unblock_mask(task, term_mask);

		TEST_ASSERT_EQ(send_group_signal(SIGTERM, task), 0);
		do_signal(&tf);
		TEST_ASSERT_EQ(task_signal_state(task)->shared_pending &
				       term_mask,
			       (uint64_t)0);
		TEST_ASSERT_EQ(send_group_signal(SIGKILL, task), 0);
		TEST_ASSERT(!signal_fatal_pending(task));
		do_signal(&tf);
		TEST_ASSERT_EQ(task_signal_state(task)->shared_pending &
				       signal_mask(SIGKILL),
			       (uint64_t)0);

		task_sighand(task)->sigactions[SIGUSR1].sa_handler =
			(__sighandler_t)0x1000;
		TEST_ASSERT_EQ(send_group_signal(SIGUSR1, task), 0);
		TEST_ASSERT_EQ(task_signal_state(task)->shared_pending &
				       user_mask,
			       user_mask);

		TEST_ASSERT_EQ(send_group_signal(SIGSTOP, task), 0);
		TEST_ASSERT_EQ(task_signal_state(task)->shared_pending &
				       stop_mask,
			       stop_mask);

		TEST_ASSERT_EQ(force_signal(SIGKILL, task), 0);
		TEST_ASSERT_EQ(task_pending_mask(task) & signal_mask(SIGKILL),
			       signal_mask(SIGKILL));
		TEST_ASSERT(signal_fatal_pending(task));
	}
	TEST_END("syscall compat: init signal protection");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: init signal protection", "see above");
cleanup:
	init_task = saved_init;
	set_current_task(saved_current);
	if (task)
		task_free(task);

	return __test_ret;
}

int test_kill_all_processes(void)
{
	struct task_struct *sender = current_task();
	struct task_struct *saved_init = init_task;
	struct task_struct *target = NULL;
	struct task_struct *inherited_target = NULL;
	struct task_struct *kernel_target = NULL;
	struct trap_frame tf = {0};
	uint64_t mask = signal_mask(SIGUSR1);

	TEST_BEGIN("syscall compat: kill all processes");
	{
		target = task_alloc();
		TEST_ASSERT_NOT_NULL(target);
		TEST_ASSERT_EQ(task_init_resources(target), 0);
		task_test_mark_user_process(target);
		task_publish(target);
		inherited_target = task_alloc();
		TEST_ASSERT_NOT_NULL(inherited_target);
		TEST_ASSERT_EQ(task_init_resources(inherited_target), 0);
		task_test_inherit_process_role(inherited_target, target);
		task_publish(inherited_target);
		kernel_target = task_alloc();
		TEST_ASSERT_NOT_NULL(kernel_target);
		TEST_ASSERT_EQ(task_init_resources(kernel_target), 0);
		init_task = sender;
		signal_clear_pending(sender, mask);

		tf.a0 = (uint64_t)-1;
		tf.a1 = SIGUSR1;
		TEST_ASSERT_EQ(sys_kill(&tf), 0);
		TEST_ASSERT_EQ(task_signal_state(target)->shared_pending & mask,
			       mask);
		TEST_ASSERT_EQ(
			task_signal_state(inherited_target)->shared_pending &
				mask,
			mask);
		TEST_ASSERT_EQ(task_signal_state(sender)->shared_pending & mask,
			       (uint64_t)0);
		TEST_ASSERT_EQ(task_pending_mask(sender) & mask, (uint64_t)0);
		TEST_ASSERT_EQ(
			task_signal_state(kernel_target)->shared_pending & mask,
			(uint64_t)0);

		tf.a0 = (uint64_t)-2;
		TEST_ASSERT_EQ(sys_kill(&tf), -EINVAL);
	}
	TEST_END("syscall compat: kill all processes");
	goto cleanup;
fail:
	TEST_FAIL("syscall compat: kill all processes", "see above");
cleanup:
	init_task = saved_init;
	if (kernel_target)
		task_free(kernel_target);
	if (inherited_target)
		test_release_published_task(inherited_target);
	if (target) {
		test_release_published_task(target);
	}

	return __test_ret;
}

int test_shutdown_syscall_contract(void)
{
	struct trap_frame tf = {0};
	struct task_struct *task = current_task();
	uid_t saved_uid = task_uid(task);

	TEST_BEGIN("syscall compat: shutdown syscall contract");
	{
		tf.a7 = SYS_sync;
		do_syscall(&tf);
		TEST_ASSERT_EQ((ssize_t)tf.a0, 0);

		memset(&tf, 0, sizeof(tf));
		tf.a7 = SYS_reboot;
		tf.a0 = 0;
		tf.a1 = LINUX_REBOOT_MAGIC2;
		tf.a2 = LINUX_REBOOT_CMD_CAD_OFF;
		do_syscall(&tf);
		TEST_ASSERT_EQ((ssize_t)tf.a0, -EINVAL);

		memset(&tf, 0, sizeof(tf));
		tf.a7 = SYS_reboot;
		tf.a0 = LINUX_REBOOT_MAGIC1;
		tf.a1 = 0;
		tf.a2 = LINUX_REBOOT_CMD_CAD_OFF;
		do_syscall(&tf);
		TEST_ASSERT_EQ((ssize_t)tf.a0, -EINVAL);

		memset(&tf, 0, sizeof(tf));
		tf.a7 = SYS_reboot;
		tf.a0 = LINUX_REBOOT_MAGIC1;
		tf.a1 = LINUX_REBOOT_MAGIC2;
		tf.a2 = LINUX_REBOOT_CMD_CAD_OFF;
		task_set_uid(task, 1);
		do_syscall(&tf);
		TEST_ASSERT_EQ((ssize_t)tf.a0, -EPERM);
		task_set_uid(task, 0);

		memset(&tf, 0, sizeof(tf));
		tf.a7 = SYS_reboot;
		tf.a0 = LINUX_REBOOT_MAGIC1;
		tf.a1 = LINUX_REBOOT_MAGIC2A;
		tf.a2 = LINUX_REBOOT_CMD_CAD_OFF;
		do_syscall(&tf);
		TEST_ASSERT_EQ((ssize_t)tf.a0, 0);

		memset(&tf, 0, sizeof(tf));
		tf.a7 = SYS_reboot;
		tf.a0 = LINUX_REBOOT_MAGIC1;
		tf.a1 = LINUX_REBOOT_MAGIC2;
		tf.a2 = LINUX_REBOOT_CMD_CAD_ON;
		do_syscall(&tf);
		TEST_ASSERT_EQ((ssize_t)tf.a0, 0);

		memset(&tf, 0, sizeof(tf));
		tf.a7 = SYS_reboot;
		tf.a0 = LINUX_REBOOT_MAGIC1;
		tf.a1 = LINUX_REBOOT_MAGIC2;
		tf.a2 = 0xdeadbeef;
		do_syscall(&tf);
		TEST_ASSERT_EQ((ssize_t)tf.a0, -EINVAL);
	}
	TEST_END("syscall compat: shutdown syscall contract");
	task_set_uid(task, saved_uid);
	return __test_ret;
fail:
	TEST_FAIL("syscall compat: shutdown syscall contract", "see above");
	task_set_uid(task, saved_uid);

	return __test_ret;
}

int test_root_statfs_fields(void)
{
	struct statfs64 st;
	int i;

	TEST_BEGIN("syscall compat: root statfs fields");
	{
		TEST_ASSERT_NOT_NULL(root_dentry);
		TEST_ASSERT_NOT_NULL(root_dentry->d_sb);
		TEST_ASSERT_EQ(vfs_statfs(root_dentry->d_sb, &st), 0);
		TEST_ASSERT_EQ(st.f_type, (int64_t)0xef53);
		TEST_ASSERT_EQ(st.f_bsize, (int64_t)BLOCK_SIZE);
		TEST_ASSERT_EQ(st.f_frsize, (int64_t)BLOCK_SIZE);
		TEST_ASSERT(st.f_blocks > 0);
		TEST_ASSERT(st.f_bfree <= st.f_blocks);
		TEST_ASSERT(st.f_bavail <= st.f_bfree);
		TEST_ASSERT(st.f_files > 0);
		TEST_ASSERT(st.f_ffree <= st.f_files);
		TEST_ASSERT(st.f_fsid[0] != 0 || st.f_fsid[1] != 0);
		TEST_ASSERT_EQ(st.f_namelen, (int64_t)255);
		TEST_ASSERT_EQ(st.f_flags, 0);
		for (i = 0; i < 4; i++)
			TEST_ASSERT_EQ(st.f_spare[i], 0);
	}
	TEST_END("syscall compat: root statfs fields");
	return __test_ret;
fail:
	TEST_FAIL("syscall compat: root statfs fields", "see above");

	return __test_ret;
}

int test_pipe2_file_alloc_failure_cleanup(void)
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
	return __test_ret;
fail:
	pipe_test_set_file_alloc_fail_at(0);
	TEST_FAIL("syscall compat: pipe2 allocation failure cleanup",
		  "see above");

	return __test_ret;
}

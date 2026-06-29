#include <kernel/fdtable.h>
#include <kernel/fs_struct.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/test.h>

static struct task_struct *resource_test_task(void)
{
	struct task_struct *task = task_alloc();

	if (!task)
		return NULL;
	if (task_init_resources(task) < 0) {
		task_free(task);
		return NULL;
	}
	return task;
}

void test_files_struct_copy_and_share(void)
{
	struct task_struct *saved = current;
	struct task_struct *parent = NULL;
	struct task_struct *child = NULL;
	struct file *file;

	TEST_BEGIN("resources: files copy/share");
	{
		parent = resource_test_task();
		child = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_NOT_NULL(child);

		current = parent;
		TEST_ASSERT_EQ(copy_files(child, false), 0);
		TEST_ASSERT(parent->files != child->files);
		TEST_ASSERT_EQ(fd_close(KERN_STDIN), 0);
		TEST_ASSERT_NULL(fd_get(KERN_STDIN));

		current = child;
		file = fd_get(KERN_STDIN);
		TEST_ASSERT_NOT_NULL(file);
		file_put(file);

		current = saved;
		task_free(child);
		task_free(parent);
		child = NULL;
		parent = NULL;

		parent = resource_test_task();
		child = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_NOT_NULL(child);

		current = parent;
		TEST_ASSERT_EQ(copy_files(child, true), 0);
		TEST_ASSERT_EQ(parent->files, child->files);
		TEST_ASSERT_EQ(fd_close(KERN_STDIN), 0);

		current = child;
		TEST_ASSERT_NULL(fd_get(KERN_STDIN));
	}
	TEST_END("resources: files copy/share");
	goto cleanup;
fail:
	TEST_FAIL("resources: files copy/share", "see above");
cleanup:
	current = saved;
	if (child)
		task_free(child);
	if (parent)
		task_free(parent);
}

void test_files_struct_copy_preserves_cloexec(void)
{
	struct task_struct *saved = current;
	struct task_struct *parent = NULL;
	struct task_struct *child = NULL;

	TEST_BEGIN("resources: files copy preserves cloexec");
	{
		parent = resource_test_task();
		child = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_NOT_NULL(child);

		parent->files->close_on_exec |= (1UL << KERN_STDIN);

		current = parent;
		TEST_ASSERT_EQ(copy_files(child, false), 0);
		TEST_ASSERT(parent->files != child->files);
		TEST_ASSERT_EQ(child->files->close_on_exec,
			       parent->files->close_on_exec);

		current = saved;
		task_free(child);
		task_free(parent);
		child = NULL;
		parent = NULL;

		parent = resource_test_task();
		child = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_NOT_NULL(child);

		parent->files->close_on_exec |= (1UL << KERN_STDIN);

		current = parent;
		TEST_ASSERT_EQ(copy_files(child, true), 0);
		TEST_ASSERT_EQ(parent->files, child->files);
		TEST_ASSERT_EQ(child->files->close_on_exec,
			       parent->files->close_on_exec);
	}
	TEST_END("resources: files copy preserves cloexec");
	goto cleanup;
fail:
	TEST_FAIL("resources: files copy preserves cloexec", "see above");
cleanup:
	current = saved;
	if (child)
		task_free(child);
	if (parent)
		task_free(parent);
}

void test_fs_struct_copy_and_share(void)
{
	struct task_struct *saved = current;
	struct task_struct *parent = NULL;
	struct task_struct *child = NULL;

	TEST_BEGIN("resources: fs copy/share");
	{
		parent = resource_test_task();
		child = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_NOT_NULL(child);

		current = parent;
		TEST_ASSERT_EQ(fs_set_umask(parent->fs, 0077), 0022);
		TEST_ASSERT_EQ(copy_fs(child, false), 0);
		TEST_ASSERT(parent->fs != child->fs);
		TEST_ASSERT_EQ(fs_set_umask(parent->fs, 0002), 0077);
		TEST_ASSERT_EQ(fs_get_umask(child->fs), (uint32_t)0077);

		current = saved;
		task_free(child);
		task_free(parent);
		child = NULL;
		parent = NULL;

		parent = resource_test_task();
		child = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_NOT_NULL(child);

		current = parent;
		TEST_ASSERT_EQ(copy_fs(child, true), 0);
		TEST_ASSERT_EQ(parent->fs, child->fs);
		TEST_ASSERT_EQ(fs_set_umask(parent->fs, 0007), 0022);
		TEST_ASSERT_EQ(fs_get_umask(child->fs), (uint32_t)0007);
	}
	TEST_END("resources: fs copy/share");
	goto cleanup;
fail:
	TEST_FAIL("resources: fs copy/share", "see above");
cleanup:
	current = saved;
	if (child)
		task_free(child);
	if (parent)
		task_free(parent);
}

void test_sighand_struct_copy_and_share(void)
{
	struct task_struct *saved = current;
	struct task_struct *parent = NULL;
	struct task_struct *child = NULL;
	__sighandler_t handler = (__sighandler_t)(uintptr_t)0x12340000UL;

	TEST_BEGIN("resources: sighand copy/share");
	{
		parent = resource_test_task();
		child = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_NOT_NULL(child);

		current = parent;
		parent->blocked = signal_mask(SIGUSR2);
		TEST_ASSERT_EQ(signals_clone(child, false, false, false), 0);
		TEST_ASSERT(parent->sighand != child->sighand);
		TEST_ASSERT(parent->signal != child->signal);
		TEST_ASSERT_EQ(child->blocked, signal_mask(SIGUSR2));

		mutex_lock(&parent->sighand->lock);
		parent->sighand->sigactions[SIGUSR1].sa_handler = handler;
		mutex_unlock(&parent->sighand->lock);
		TEST_ASSERT_NE(child->sighand->sigactions[SIGUSR1].sa_handler,
			       handler);

		parent->blocked = signal_mask(SIGTERM);
		TEST_ASSERT_EQ(child->blocked, signal_mask(SIGUSR2));

		current = saved;
		task_free(child);
		task_free(parent);
		child = NULL;
		parent = NULL;

		parent = resource_test_task();
		child = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_NOT_NULL(child);

		current = parent;
		TEST_ASSERT_EQ(signals_clone(child, true, true, false), 0);
		TEST_ASSERT_EQ(parent->sighand, child->sighand);
		TEST_ASSERT_EQ(parent->signal, child->signal);

		mutex_lock(&parent->sighand->lock);
		parent->sighand->sigactions[SIGUSR1].sa_handler = handler;
		mutex_unlock(&parent->sighand->lock);
		TEST_ASSERT_EQ(child->sighand->sigactions[SIGUSR1].sa_handler,
			       handler);

		parent->blocked = signal_mask(SIGTERM);
		TEST_ASSERT_EQ(child->blocked, (uint64_t)0);
	}
	TEST_END("resources: sighand copy/share");
	goto cleanup;
fail:
	TEST_FAIL("resources: sighand copy/share", "see above");
cleanup:
	current = saved;
	if (child)
		task_free(child);
	if (parent)
		task_free(parent);
}

void test_signal_struct_pending(void)
{
	struct task_struct *saved = current;
	struct task_struct *parent = NULL;
	struct task_struct *child = NULL;

	TEST_BEGIN("resources: signal pending");
	{
		parent = resource_test_task();
		child = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_NOT_NULL(child);

		current = parent;
		TEST_ASSERT_EQ(signals_clone(child, true, true, false), 0);
		TEST_ASSERT_EQ(send_signal(SIGUSR1, child), 0);
		TEST_ASSERT_EQ(child->pending, signal_mask(SIGUSR1));
		TEST_ASSERT_EQ(parent->pending, (uint64_t)0);

		TEST_ASSERT_EQ(send_group_signal(SIGUSR2, parent), 0);
		TEST_ASSERT_EQ(parent->signal->shared_pending,
			       signal_mask(SIGUSR2));
		TEST_ASSERT_EQ(child->signal->shared_pending,
			       signal_mask(SIGUSR2));
	}
	TEST_END("resources: signal pending");
	goto cleanup;
fail:
	TEST_FAIL("resources: signal pending", "see above");
cleanup:
	current = saved;
	if (child)
		task_free(child);
	if (parent)
		task_free(parent);
}

void test_signal_struct_rlimits_copy(void)
{
	struct task_struct *saved = current;
	struct task_struct *parent = NULL;
	struct task_struct *child = NULL;

	TEST_BEGIN("resources: signal rlimits copy");
	{
		parent = resource_test_task();
		child = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);
		TEST_ASSERT_NOT_NULL(child);

		current = parent;
		parent->signal->rlimits[RLIMIT_NOFILE].rlim_cur = 16;
		parent->signal->rlimits[RLIMIT_NOFILE].rlim_max = 16;
		TEST_ASSERT_EQ(signals_clone(child, false, false, false), 0);
		TEST_ASSERT(parent->signal != child->signal);
		TEST_ASSERT_EQ(child->signal->rlimits[RLIMIT_NOFILE].rlim_cur,
			       (uint64_t)16);
		TEST_ASSERT_EQ(child->signal->rlimits[RLIMIT_NOFILE].rlim_max,
			       (uint64_t)16);
	}
	TEST_END("resources: signal rlimits copy");
	goto cleanup;
fail:
	TEST_FAIL("resources: signal rlimits copy", "see above");
cleanup:
	current = saved;
	if (child)
		task_free(child);
	if (parent)
		task_free(parent);
}

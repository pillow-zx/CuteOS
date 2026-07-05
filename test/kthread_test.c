#include <kernel/test.h>
#include <kernel/sched.h>

static void dummy_thread_fn(void *arg)
{
	(void)arg;
	/* 不会被实际执行（测试中不调用 schedule） */
}

void test_kernel_thread_basic(void)
{
	TEST_BEGIN("kthread: basic create");
	{
		/* 先记录队列状态 */
		int was_empty = sched_test_runqueue_empty();

		struct task_struct *t = kernel_thread(dummy_thread_fn, NULL);
		TEST_ASSERT_NOT_NULL(t);
		TEST_ASSERT(task_pid(t) > 0);
		TEST_ASSERT_EQ(task_state(t), (uint32_t)TASK_RUNNING);
		TEST_ASSERT_NOT_NULL(task_kernel_stack(t));

		/* kernel_thread 应已将任务入队 */
		TEST_ASSERT(!sched_test_runqueue_empty());

		/* 清理：出队并释放 */
		sched_dequeue(t);
		task_free(t);

		/* 队列应恢复原状 */
		if (was_empty)
			TEST_ASSERT(sched_test_runqueue_empty());
	}
	TEST_END("kthread: basic create");
	return;
fail:
	TEST_FAIL("kthread: basic create", "see above");
}

void test_kernel_thread_ctx_setup(void)
{
	TEST_BEGIN("kthread: ctx and trap_frame setup");
	{
		int test_arg_val = 0x1234;
		struct task_struct *t = kernel_thread(
			dummy_thread_fn, (void *)(size_t)test_arg_val);
		TEST_ASSERT_NOT_NULL(t);

		TEST_ASSERT(arch_task_test_kernel_thread_setup(
			t, dummy_thread_fn, (void *)(size_t)test_arg_val));

		/* 清理 */
		sched_dequeue(t);
		task_free(t);
	}
	TEST_END("kthread: ctx and trap_frame setup");
	return;
fail:
	TEST_FAIL("kthread: ctx and trap_frame setup", "see above");
}

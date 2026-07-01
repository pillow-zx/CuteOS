#include <kernel/test.h>
#include <kernel/sched.h>

/* entry.S 中的 trap 返回入口，用于验证 ctx.ra 指向正确地址 */
extern void __trapret(void);

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
		TEST_ASSERT(t->ids.pid > 0);
		TEST_ASSERT_EQ(t->lifecycle.state, (uint32_t)TASK_RUNNING);
		TEST_ASSERT(t->arch.kstack != NULL);

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

		/* ctx.ra 应指向 __trapret */
		TEST_ASSERT_EQ(t->arch.ctx.ra, (size_t)__trapret);

		/* ctx.sp 应指向栈顶的 trap_frame */
		struct trap_frame *expected_tf =
			(struct trap_frame *)((uint8_t *)t->arch.kstack +
					      KSTACK_SIZE -
					      sizeof(struct trap_frame));
		TEST_ASSERT_EQ(t->arch.ctx.sp, (size_t)expected_tf);

		/* tf 指针正确 */
		TEST_ASSERT_EQ((size_t)t->arch.tf, (size_t)expected_tf);

		/* sepc 指向入口函数 */
		TEST_ASSERT_EQ(t->arch.tf->sepc, (size_t)dummy_thread_fn);

		/* a0 传递了 arg */
		TEST_ASSERT_EQ(t->arch.tf->a0, (size_t)test_arg_val);

		/* sstatus 包含 SPP 和 SPIE */
		TEST_ASSERT(t->arch.tf->sstatus & SSTATUS_SPP);
		TEST_ASSERT(t->arch.tf->sstatus & SSTATUS_SPIE);

		/* 清理 */
		sched_dequeue(t);
		task_free(t);
	}
	TEST_END("kthread: ctx and trap_frame setup");
	return;
fail:
	TEST_FAIL("kthread: ctx and trap_frame setup", "see above");
}

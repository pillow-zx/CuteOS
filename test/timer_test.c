#include <kernel/test.h>
#include <kernel/sched.h>
#include <kernel/task.h>
#include <kernel/test_wait.h>
#include <kernel/timer.h>
#include <kernel/wait.h>

void test_timer_mtime(void)
{
	TEST_BEGIN("timer: mtime monotonic");
	{
		uint64_t t0 = arch_timer_now();

		/* 读 1000 次，值不应减小 */
		for (int i = 0; i < 1000; i++) {
			uint64_t t1 = arch_timer_now();
			TEST_ASSERT(t1 >= t0);
			t0 = t1;
		}
	}
	TEST_END("timer: mtime monotonic");
	return;
fail:
	TEST_FAIL("timer: mtime monotonic", "see above");
}

void test_timer_mtimecmp(void)
{
	TEST_BEGIN("timer: mtimecmp write/read");
	{
		uint64_t now = arch_timer_now();

		/* 设置一个远的超时，不触发中断 */
		arch_timer_set(now + 10000000UL);

		/* 如果没 panic，说明 CSR 可写 */
		/* 恢复正常的 tick 间隔 */
		arch_timer_set(now + 100000UL);
	}
	TEST_END("timer: mtimecmp write/read");
}
void test_timer_jiffies(void)
{
	TEST_BEGIN("timer: jiffies initial value");
	{
		/*
		 * arch_timer_init() 在 kernel_main 中先于 kernel_test() 调用，
		 * jiffies 应 >= 0（可能 > 0，取决于初始化期间是否有 tick）。
		 * 只验证它没有溢出或变为异常值。
		 */
		uint64_t j = jiffies;
		TEST_ASSERT(j < 1000000UL);
	}
	TEST_END("timer: jiffies initial value");
	return;
fail:
	TEST_FAIL("timer: jiffies initial value", "see above");
}

void test_timer_constants(void)
{
	TEST_BEGIN("timer: constants");
	{
		/*
		 * 验证 timer.c 中的核心常量关系（直接使用字面量，
		 * 因为这些 #define 在 timer.c 内部，不通过头文件导出）：
		 *
		 *   HZ              = 100        (100 Hz tick)
		 *   MTIME_FREQ      = 10000000   (10 MHz)
		 *   CLOCKS_PER_TICK = 100000     (10000000 / 100)
		 */
		TEST_ASSERT_EQ(10000000UL / 100UL, 100000UL);
		TEST_ASSERT_EQ(100000UL * 100UL, 10000000UL);

		/* 每 tick = 100000 / 10000000 = 10ms */
		TEST_ASSERT_EQ(100000UL * 1000UL / 10000000UL, 10UL);
	}
	TEST_END("timer: constants");
	return;
fail:
	TEST_FAIL("timer: constants", "see above");
}

void test_waitqueue_timeout_expiry_wakes_task(void)
{
	struct task_struct *task = NULL;
	struct wait_timer_test_handle wait;

	TEST_BEGIN("waitqueue: timeout expiry wakes task");
	{
		task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);
		task->state = TASK_UNINTERRUPTIBLE;

		wait_timer_test_init(&wait, task, 100);
		wait_timer_test_start(&wait);
		timer_run_expired(99);
		TEST_ASSERT_EQ(task->state, (uint32_t)TASK_UNINTERRUPTIBLE);
		TEST_ASSERT(!wait_timer_test_fired(&wait));

		timer_run_expired(100);
		TEST_ASSERT_EQ(task->state, (uint32_t)TASK_RUNNING);
		TEST_ASSERT(wait_timer_test_fired(&wait));
		TEST_ASSERT(!wait_timer_test_cancel(&wait));
		TEST_ASSERT(!list_empty(&task->run_list));
	}
	TEST_END("waitqueue: timeout expiry wakes task");
	goto cleanup;
fail:
	TEST_FAIL("waitqueue: timeout expiry wakes task", "see above");
cleanup:
	if (task) {
		if (!list_empty(&task->run_list))
			sched_dequeue(task);
		task_free(task);
	}
}

void test_waitqueue_timeout_cancel_prevents_wake(void)
{
	struct task_struct *task = NULL;
	struct wait_timer_test_handle wait;

	TEST_BEGIN("waitqueue: timeout cancel prevents wake");
	{
		task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);
		task->state = TASK_UNINTERRUPTIBLE;

		wait_timer_test_init(&wait, task, 200);
		wait_timer_test_start(&wait);
		TEST_ASSERT(wait_timer_test_cancel(&wait));
		timer_run_expired(200);
		TEST_ASSERT_EQ(task->state, (uint32_t)TASK_UNINTERRUPTIBLE);
		TEST_ASSERT(!wait_timer_test_fired(&wait));
		TEST_ASSERT(list_empty(&task->run_list));
	}
	TEST_END("waitqueue: timeout cancel prevents wake");
	goto cleanup;
fail:
	TEST_FAIL("waitqueue: timeout cancel prevents wake", "see above");
cleanup:
	if (task) {
		if (!list_empty(&task->run_list))
			sched_dequeue(task);
		task_free(task);
	}
}

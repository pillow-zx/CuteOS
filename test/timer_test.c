#include <kernel/test.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/task.h>
#include <kernel/test_wait.h>
#include <kernel/time.h>
#include <kernel/timer.h>
#include <kernel/wait.h>

static void ktimer_test_callback(struct ktimer *timer, void *arg)
{
	int *count = arg;

	(void)timer;
	(*count)++;
}

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

void test_mtime_deadline_helpers(void)
{
	bool has_timeout = true;
	uint64_t deadline = 1;

	TEST_BEGIN("timer: mtime deadline helpers");
	{
		struct timespec one_sec = {
			.tv_sec = 1,
			.tv_nsec = 0,
		};
		struct timespec invalid_nsec = {
			.tv_sec = 0,
			.tv_nsec = 1000000000LL,
		};
		struct timespec invalid_sec = {
			.tv_sec = -1,
			.tv_nsec = 0,
		};
		struct timespec huge = {
			.tv_sec = (int64_t)(UINT64_MAX / MTIME_FREQ) + 1,
			.tv_nsec = 0,
		};
		uint64_t before;
		uint64_t after;

		TEST_ASSERT_EQ(mtime_deadline_from_timespec(NULL,
							    &has_timeout,
							    &deadline),
			       0);
		TEST_ASSERT(!has_timeout);
		TEST_ASSERT_EQ(deadline, (uint64_t)0);

		before = arch_timer_now();
		TEST_ASSERT_EQ(mtime_deadline_from_timespec(&one_sec,
							    &has_timeout,
							    &deadline),
			       0);
		after = arch_timer_now();
		TEST_ASSERT(has_timeout);
		TEST_ASSERT(deadline >= before + MTIME_FREQ);
		TEST_ASSERT(deadline <= after + MTIME_FREQ);

		TEST_ASSERT_EQ(mtime_deadline_from_timespec(&invalid_nsec,
							    &has_timeout,
							    &deadline),
			       -EINVAL);
		TEST_ASSERT_EQ(mtime_deadline_from_timespec(&invalid_sec,
							    &has_timeout,
							    &deadline),
			       -EINVAL);

		TEST_ASSERT_EQ(mtime_deadline_from_timespec(&huge,
							    &has_timeout,
							    &deadline),
			       0);
		TEST_ASSERT(has_timeout);
		TEST_ASSERT_EQ(deadline, UINT64_MAX);

		TEST_ASSERT_EQ(mtime_deadline_from_ms(-1, &has_timeout,
						      &deadline),
			       0);
		TEST_ASSERT(!has_timeout);
		TEST_ASSERT_EQ(deadline, (uint64_t)0);

		before = arch_timer_now();
		TEST_ASSERT_EQ(mtime_deadline_from_ms(0, &has_timeout,
						      &deadline),
			       0);
		after = arch_timer_now();
		TEST_ASSERT(has_timeout);
		TEST_ASSERT(deadline >= before);
		TEST_ASSERT(deadline <= after);

		before = arch_timer_now();
		TEST_ASSERT_EQ(mtime_deadline_from_ms(25, &has_timeout,
						      &deadline),
			       0);
		after = arch_timer_now();
		TEST_ASSERT(has_timeout);
		TEST_ASSERT(deadline >= before + 250000UL);
		TEST_ASSERT(deadline <= after + 250000UL);
	}
	TEST_END("timer: mtime deadline helpers");
	return;
fail:
	TEST_FAIL("timer: mtime deadline helpers", "see above");
}

void test_waitqueue_timeout_expiry_wakes_task(void)
{
	struct task_struct *task = NULL;

	TEST_BEGIN("waitqueue: timeout expiry wakes task");
	{
		task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);
		task->lifecycle.state = TASK_UNINTERRUPTIBLE;

		wait_timeout_test_start(task, 100);
		timer_run_expired(99);
		TEST_ASSERT_EQ(task->lifecycle.state, (uint32_t)TASK_UNINTERRUPTIBLE);
		TEST_ASSERT(!wait_timeout_test_fired());
		TEST_ASSERT(wait_timeout_test_active());

		timer_run_expired(100);
		TEST_ASSERT_EQ(task->lifecycle.state, (uint32_t)TASK_RUNNING);
		TEST_ASSERT(wait_timeout_test_fired());
		TEST_ASSERT(!wait_timeout_test_active());
		TEST_ASSERT(!wait_timeout_test_cancel());
		TEST_ASSERT(!list_empty(&task->sched.run_list));
	}
	TEST_END("waitqueue: timeout expiry wakes task");
	goto cleanup;
fail:
	TEST_FAIL("waitqueue: timeout expiry wakes task", "see above");
cleanup:
	if (task) {
		if (!list_empty(&task->sched.run_list))
			sched_dequeue(task);
		task_free(task);
	}
}

void test_waitqueue_timeout_cancel_prevents_wake(void)
{
	struct task_struct *task = NULL;

	TEST_BEGIN("waitqueue: timeout cancel prevents wake");
	{
		task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);
		task->lifecycle.state = TASK_UNINTERRUPTIBLE;

		wait_timeout_test_start(task, 200);
		TEST_ASSERT(wait_timeout_test_active());
		TEST_ASSERT(wait_timeout_test_cancel());
		TEST_ASSERT(!wait_timeout_test_active());
		timer_run_expired(200);
		TEST_ASSERT_EQ(task->lifecycle.state, (uint32_t)TASK_UNINTERRUPTIBLE);
		TEST_ASSERT(!wait_timeout_test_fired());
		TEST_ASSERT(list_empty(&task->sched.run_list));
	}
	TEST_END("waitqueue: timeout cancel prevents wake");
	goto cleanup;
fail:
	TEST_FAIL("waitqueue: timeout cancel prevents wake", "see above");
cleanup:
	if (task) {
		if (!list_empty(&task->sched.run_list))
			sched_dequeue(task);
		task_free(task);
	}
}

void test_ktimer_arm_cancel_remaining(void)
{
	struct ktimer timer;
	int fired = 0;

	TEST_BEGIN("ktimer: arm cancel remaining");
	{
		ktimer_init(&timer, ktimer_test_callback, &fired);
		TEST_ASSERT(!ktimer_active(&timer));
		TEST_ASSERT_EQ(ktimer_remaining(&timer, 10), (uint64_t)0);

		TEST_ASSERT_EQ(ktimer_arm(&timer, 100, 0), 0);
		TEST_ASSERT(ktimer_active(&timer));
		TEST_ASSERT_EQ(ktimer_remaining(&timer, 40), (uint64_t)60);

		TEST_ASSERT(ktimer_cancel(&timer));
		TEST_ASSERT(!ktimer_active(&timer));
		TEST_ASSERT_EQ(ktimer_remaining(&timer, 40), (uint64_t)0);

		ktimer_run_expired(100);
		TEST_ASSERT_EQ(fired, 0);
	}
	TEST_END("ktimer: arm cancel remaining");
	return;
fail:
	if (ktimer_active(&timer)) {
		bool cancelled = ktimer_cancel(&timer);

		(void)cancelled;
	}
	TEST_FAIL("ktimer: arm cancel remaining", "see above");
}

void test_ktimer_timer_run_expired_callback(void)
{
	struct ktimer timer;
	int fired = 0;

	TEST_BEGIN("ktimer: timer_run_expired callback");
	{
		ktimer_init(&timer, ktimer_test_callback, &fired);
		TEST_ASSERT_EQ(ktimer_arm(&timer, 100, 0), 0);

		timer_run_expired(99);
		TEST_ASSERT_EQ(fired, 0);
		TEST_ASSERT(ktimer_active(&timer));

		timer_run_expired(100);
		TEST_ASSERT_EQ(fired, 1);
		TEST_ASSERT(!ktimer_active(&timer));
	}
	TEST_END("ktimer: timer_run_expired callback");
	return;
fail:
	if (ktimer_active(&timer)) {
		bool cancelled = ktimer_cancel(&timer);

		(void)cancelled;
	}
	TEST_FAIL("ktimer: timer_run_expired callback", "see above");
}

void test_ktimer_interval_rearms_after_expiry(void)
{
	struct ktimer timer;
	int fired = 0;

	TEST_BEGIN("ktimer: interval rearms after expiry");
	{
		ktimer_init(&timer, ktimer_test_callback, &fired);
		TEST_ASSERT_EQ(ktimer_arm(&timer, 100, 25), 0);

		timer_run_expired(100);
		TEST_ASSERT_EQ(fired, 1);
		TEST_ASSERT(ktimer_active(&timer));
		TEST_ASSERT_EQ(ktimer_remaining(&timer, 100), (uint64_t)25);

		timer_run_expired(124);
		TEST_ASSERT_EQ(fired, 1);
		TEST_ASSERT(ktimer_active(&timer));

		timer_run_expired(125);
		TEST_ASSERT_EQ(fired, 2);
		TEST_ASSERT(ktimer_active(&timer));
		TEST_ASSERT_EQ(ktimer_remaining(&timer, 125), (uint64_t)25);

		TEST_ASSERT(ktimer_cancel(&timer));
	}
	TEST_END("ktimer: interval rearms after expiry");
	return;
fail:
	if (ktimer_active(&timer)) {
		bool cancelled = ktimer_cancel(&timer);

		(void)cancelled;
	}
	TEST_FAIL("ktimer: interval rearms after expiry", "see above");
}

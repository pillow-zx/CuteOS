#include <kernel/sched.h>
#include <kernel/test.h>
#include <kernel/timer.h>

int test_sched_init(void)
{
	TEST_BEGIN("sched: MLFQ init");
	{
		TEST_ASSERT(sched_test_runqueue_empty());
		TEST_ASSERT_EQ(sched_test_runnable_count(), (uint32_t)0);
	}
	TEST_END("sched: MLFQ init");
	return __test_ret;
fail:
	TEST_FAIL("sched: MLFQ init", "see above");

	return __test_ret;
}

int test_sched_enqueue_dequeue(void)
{
	TEST_BEGIN("sched: MLFQ enqueue/dequeue order");
	{
		struct task_struct *t1 = task_alloc();
		struct task_struct *t2 = task_alloc();
		struct task_struct *t3 = task_alloc();
		TEST_ASSERT_NOT_NULL(t1);
		TEST_ASSERT_NOT_NULL(t2);
		TEST_ASSERT_NOT_NULL(t3);

		sched_test_set_level(t1, 2);
		sched_test_set_level(t2, 0);
		sched_test_set_level(t3, 1);

		sched_enqueue(t1);
		sched_enqueue(t3);
		sched_enqueue(t2);
		TEST_ASSERT(!sched_test_runqueue_empty());
		TEST_ASSERT_EQ(sched_test_runnable_count(), (uint32_t)3);

		TEST_ASSERT_EQ(task_pid(sched_test_peek_next()), task_pid(t2));
		sched_dequeue(t2);
		TEST_ASSERT_EQ(task_pid(sched_test_peek_next()), task_pid(t3));
		sched_dequeue(t3);
		TEST_ASSERT_EQ(task_pid(sched_test_peek_next()), task_pid(t1));
		sched_dequeue(t1);

		TEST_ASSERT(sched_test_runqueue_empty());

		task_free(t1);
		task_free(t2);
		task_free(t3);
	}
	TEST_END("sched: MLFQ enqueue/dequeue order");
	return __test_ret;
fail:
	TEST_FAIL("sched: MLFQ enqueue/dequeue order", "see above");

	return __test_ret;
}

int test_sched_need_resched(void)
{
	struct task_struct *t = NULL;
	struct task_struct *saved_current = current_task();
	uint64_t saved_jiffies = jiffies;

	TEST_BEGIN("sched: tick demotes and requests resched");
	{
		t = task_alloc();
		TEST_ASSERT_NOT_NULL(t);

		TEST_ASSERT_EQ(sched_test_need_resched(t), (uint8_t)0);
		TEST_ASSERT_EQ(sched_test_level(t), (uint8_t)0);
		TEST_ASSERT_EQ(sched_test_time_slice(t),
			       sched_test_level_slice(0));

		set_current_task(t);
		jiffies = 1;
		sched_tick();

		TEST_ASSERT_EQ(sched_test_level(t), (uint8_t)1);
		TEST_ASSERT_EQ(sched_test_time_slice(t),
			       sched_test_level_slice(1));
		TEST_ASSERT_EQ(sched_test_need_resched(t), (uint8_t)1);
	}
	TEST_END("sched: tick demotes and requests resched");
	goto cleanup;
fail:
	TEST_FAIL("sched: tick demotes and requests resched", "see above");
cleanup:
	set_current_task(saved_current);
	jiffies = saved_jiffies;
	if (t)
		task_free(t);

	return __test_ret;
}

int test_sched_preempt_count_is_cpu_local(void)
{
	int saved = cpu_preempt_count(current_cpu());

	TEST_BEGIN("sched: preempt count is CPU-local");
	{
		cpu_set_preempt_count(current_cpu(), 0);
		TEST_ASSERT(preemptible());

		preempt_disable();
		TEST_ASSERT_EQ(cpu_preempt_count(current_cpu()), 1);
		TEST_ASSERT(!preemptible());

		preempt_enable();
		TEST_ASSERT_EQ(cpu_preempt_count(current_cpu()), 0);
		TEST_ASSERT(preemptible());
	}
	TEST_END("sched: preempt count is CPU-local");
	goto cleanup;
fail:
	TEST_FAIL("sched: preempt count is CPU-local", "see above");
cleanup:
	cpu_set_preempt_count(current_cpu(), saved);

	return __test_ret;
}

int test_sched_wakeup_refresh(void)
{
	TEST_BEGIN("sched: wakeup refreshes without duplicate enqueue");
	{
		struct task_struct *t = task_alloc();
		TEST_ASSERT_NOT_NULL(t);

		sched_test_set_level(t, 2);
		sched_test_set_budget(t, 0, 3);

		sched_wakeup(t);
		TEST_ASSERT_EQ(sched_test_runnable_count(), (uint32_t)1);
		TEST_ASSERT_EQ(sched_test_level(t), (uint8_t)2);
		TEST_ASSERT_EQ(sched_test_time_slice(t),
			       sched_test_level_slice(2));
		TEST_ASSERT_EQ(sched_test_ticks(t), (uint8_t)0);

		sched_wakeup(t);
		TEST_ASSERT_EQ(sched_test_runnable_count(), (uint32_t)1);

		sched_dequeue(t);
		task_free(t);
	}
	TEST_END("sched: wakeup refreshes without duplicate enqueue");
	return __test_ret;
fail:
	TEST_FAIL("sched: wakeup refreshes without duplicate enqueue",
		  "see above");

	return __test_ret;
}

int test_sched_boost(void)
{
	struct task_struct *t1 = NULL;
	struct task_struct *t2 = NULL;
	struct task_struct *t3 = NULL;
	struct task_struct *saved_current = current_task();

	TEST_BEGIN("sched: periodic boost");
	{
		t1 = task_alloc();
		t2 = task_alloc();
		t3 = task_alloc();
		TEST_ASSERT_NOT_NULL(t1);
		TEST_ASSERT_NOT_NULL(t2);
		TEST_ASSERT_NOT_NULL(t3);

		sched_test_set_level(t1, 3);
		sched_test_set_budget(t1, 0, 0);
		sched_enqueue(t1);
		sched_test_set_level(t2, 2);
		sched_test_set_budget(t2, 0, 0);
		sched_enqueue(t2);
		sched_test_set_level(t3, 3);
		sched_test_set_budget(t3, 0, 0);
		set_current_task(t3);

		sched_test_force_boost();

		TEST_ASSERT_EQ(sched_test_level(t1), (uint8_t)0);
		TEST_ASSERT_EQ(sched_test_level(t2), (uint8_t)0);
		TEST_ASSERT_EQ(sched_test_level(t3), (uint8_t)0);
		TEST_ASSERT_EQ(sched_test_time_slice(t1),
			       sched_test_level_slice(0));
		TEST_ASSERT_EQ(sched_test_time_slice(t2),
			       sched_test_level_slice(0));
		TEST_ASSERT_EQ(sched_test_time_slice(t3),
			       sched_test_level_slice(0));
		TEST_ASSERT_EQ(sched_test_runnable_count(), (uint32_t)2);
	}
	TEST_END("sched: periodic boost");
	goto cleanup;
fail:
	TEST_FAIL("sched: periodic boost", "see above");
cleanup:
	set_current_task(saved_current);
	if (t1) {
		sched_dequeue(t1);
		task_free(t1);
	}
	if (t2) {
		sched_dequeue(t2);
		task_free(t2);
	}
	if (t3)
		task_free(t3);

	return __test_ret;
}

#include <kernel/sched.h>
#include <kernel/test.h>
#include <kernel/timer.h>

void test_sched_init(void)
{
	TEST_BEGIN("sched: MLFQ init");
	{
		TEST_ASSERT(sched_test_runqueue_empty());
		TEST_ASSERT_EQ(sched_test_runnable_count(), (uint32_t)0);
	}
	TEST_END("sched: MLFQ init");
	return;
fail:
	TEST_FAIL("sched: MLFQ init", "see above");
}

void test_sched_enqueue_dequeue(void)
{
	TEST_BEGIN("sched: MLFQ enqueue/dequeue order");
	{
		struct task_struct *t1 = task_alloc();
		struct task_struct *t2 = task_alloc();
		struct task_struct *t3 = task_alloc();
		TEST_ASSERT_NOT_NULL(t1);
		TEST_ASSERT_NOT_NULL(t2);
		TEST_ASSERT_NOT_NULL(t3);

		t1->sched_level = 2;
		t1->time_slice = sched_test_level_slice(t1->sched_level);
		t2->sched_level = 0;
		t2->time_slice = sched_test_level_slice(t2->sched_level);
		t3->sched_level = 1;
		t3->time_slice = sched_test_level_slice(t3->sched_level);

		sched_enqueue(t1);
		sched_enqueue(t3);
		sched_enqueue(t2);
		TEST_ASSERT(!sched_test_runqueue_empty());
		TEST_ASSERT_EQ(sched_test_runnable_count(), (uint32_t)3);

		TEST_ASSERT_EQ(sched_test_peek_next()->pid, t2->pid);
		sched_dequeue(t2);
		TEST_ASSERT_EQ(sched_test_peek_next()->pid, t3->pid);
		sched_dequeue(t3);
		TEST_ASSERT_EQ(sched_test_peek_next()->pid, t1->pid);
		sched_dequeue(t1);

		TEST_ASSERT(sched_test_runqueue_empty());

		task_free(t1);
		task_free(t2);
		task_free(t3);
	}
	TEST_END("sched: MLFQ enqueue/dequeue order");
	return;
fail:
	TEST_FAIL("sched: MLFQ enqueue/dequeue order", "see above");
}

void test_sched_need_resched(void)
{
	struct task_struct *t = NULL;
	struct task_struct *saved_current = current;
	uint64_t saved_jiffies = jiffies;

	TEST_BEGIN("sched: tick demotes and requests resched");
	{
		t = task_alloc();
		TEST_ASSERT_NOT_NULL(t);

		TEST_ASSERT_EQ(t->need_resched, (uint8_t)0);
		TEST_ASSERT_EQ(t->sched_level, (uint8_t)0);
		TEST_ASSERT_EQ(t->time_slice, sched_test_level_slice(0));

		current = t;
		jiffies = 1;
		sched_tick();

		TEST_ASSERT_EQ(t->sched_level, (uint8_t)1);
		TEST_ASSERT_EQ(t->time_slice, sched_test_level_slice(1));
		TEST_ASSERT_EQ(t->need_resched, (uint8_t)1);
	}
	TEST_END("sched: tick demotes and requests resched");
	goto cleanup;
fail:
	TEST_FAIL("sched: tick demotes and requests resched", "see above");
cleanup:
	current = saved_current;
	jiffies = saved_jiffies;
	if (t)
		task_free(t);
}

void test_sched_wakeup_refresh(void)
{
	TEST_BEGIN("sched: wakeup refreshes without duplicate enqueue");
	{
		struct task_struct *t = task_alloc();
		TEST_ASSERT_NOT_NULL(t);

		t->sched_level = 2;
		t->time_slice = 0;
		t->sched_ticks = 3;

		sched_wakeup(t);
		TEST_ASSERT_EQ(sched_test_runnable_count(), (uint32_t)1);
		TEST_ASSERT_EQ(t->sched_level, (uint8_t)2);
		TEST_ASSERT_EQ(t->time_slice, sched_test_level_slice(2));
		TEST_ASSERT_EQ(t->sched_ticks, (uint8_t)0);

		sched_wakeup(t);
		TEST_ASSERT_EQ(sched_test_runnable_count(), (uint32_t)1);

		sched_dequeue(t);
		task_free(t);
	}
	TEST_END("sched: wakeup refreshes without duplicate enqueue");
	return;
fail:
	TEST_FAIL("sched: wakeup refreshes without duplicate enqueue",
		  "see above");
}

void test_sched_boost(void)
{
	struct task_struct *t1 = NULL;
	struct task_struct *t2 = NULL;
	struct task_struct *t3 = NULL;
	struct task_struct *saved_current = current;

	TEST_BEGIN("sched: periodic boost");
	{
		t1 = task_alloc();
		t2 = task_alloc();
		t3 = task_alloc();
		TEST_ASSERT_NOT_NULL(t1);
		TEST_ASSERT_NOT_NULL(t2);
		TEST_ASSERT_NOT_NULL(t3);

		t1->sched_level = 3;
		t1->time_slice = 0;
		sched_enqueue(t1);
		t2->sched_level = 2;
		t2->time_slice = 0;
		sched_enqueue(t2);
		t3->sched_level = 3;
		t3->time_slice = 0;
		current = t3;

		sched_test_force_boost();

		TEST_ASSERT_EQ(t1->sched_level, (uint8_t)0);
		TEST_ASSERT_EQ(t2->sched_level, (uint8_t)0);
		TEST_ASSERT_EQ(t3->sched_level, (uint8_t)0);
		TEST_ASSERT_EQ(t1->time_slice, sched_test_level_slice(0));
		TEST_ASSERT_EQ(t2->time_slice, sched_test_level_slice(0));
		TEST_ASSERT_EQ(t3->time_slice, sched_test_level_slice(0));
		TEST_ASSERT_EQ(sched_test_runnable_count(), (uint32_t)2);
	}
	TEST_END("sched: periodic boost");
	goto cleanup;
fail:
	TEST_FAIL("sched: periodic boost", "see above");
cleanup:
	current = saved_current;
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
}

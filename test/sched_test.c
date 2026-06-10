#include <kernel/sched.h>
#include <kernel/test.h>

void test_sched_init(void)
{
	TEST_BEGIN("sched: runqueue init");
	{
		TEST_ASSERT(list_empty(&runqueue));
	}
	TEST_END("sched: runqueue init");
	return;
fail:
	TEST_FAIL("sched: runqueue init", "see above");
}

void test_sched_enqueue_dequeue(void)
{
	TEST_BEGIN("sched: enqueue/dequeue");
	{
		struct task_struct *t1 = task_alloc();
		struct task_struct *t2 = task_alloc();
		struct task_struct *t3 = task_alloc();
		TEST_ASSERT_NOT_NULL(t1);
		TEST_ASSERT_NOT_NULL(t2);
		TEST_ASSERT_NOT_NULL(t3);

		/* 入队三个任务 */
		sched_enqueue(t1);
		sched_enqueue(t2);
		sched_enqueue(t3);
		TEST_ASSERT(!list_empty(&runqueue));

		/* 出队应按 FIFO 顺序 */
		struct task_struct *first = list_first_entry(
			&runqueue, struct task_struct, run_list);
		TEST_ASSERT_EQ(first->pid, t1->pid);
		sched_dequeue(first);

		struct task_struct *second = list_first_entry(
			&runqueue, struct task_struct, run_list);
		TEST_ASSERT_EQ(second->pid, t2->pid);
		sched_dequeue(second);

		struct task_struct *third = list_first_entry(
			&runqueue, struct task_struct, run_list);
		TEST_ASSERT_EQ(third->pid, t3->pid);
		sched_dequeue(third);

		TEST_ASSERT(list_empty(&runqueue));

		task_free(t1);
		task_free(t2);
		task_free(t3);
	}
	TEST_END("sched: enqueue/dequeue");
	return;
fail:
	TEST_FAIL("sched: enqueue/dequeue", "see above");
}

void test_sched_need_resched(void)
{
	TEST_BEGIN("sched: need_resched field");
	{
		struct task_struct *t = task_alloc();
		TEST_ASSERT_NOT_NULL(t);

		/* 初始应为 0 */
		TEST_ASSERT_EQ(t->need_resched, (uint8_t)0);

		/* 设置和清除 */
		t->need_resched = 1;
		TEST_ASSERT_EQ(t->need_resched, (uint8_t)1);
		t->need_resched = 0;
		TEST_ASSERT_EQ(t->need_resched, (uint8_t)0);

		task_free(t);
	}
	TEST_END("sched: need_resched field");
	return;
fail:
	TEST_FAIL("sched: need_resched field", "see above");
}

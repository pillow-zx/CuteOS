#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/test.h>
#include <kernel/test_wait.h>
#include <kernel/timer.h>
#include <kernel/wait.h>

#include "../ktest.h"

static bool wait_test_ready(void *arg)
{
	bool *ready = arg;

	return *ready;
}

int test_waitqueue_timeout_expiry_wakes_task(void)
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

	return __test_ret;
}

int test_waitqueue_timeout_cancel_prevents_wake(void)
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

	return __test_ret;
}

int test_wait_event_interruptible_ready(void)
{
	struct wait_queue_head wq;
	bool ready = true;

	TEST_BEGIN("sync: wait_event_interruptible ready");
	{
		init_waitqueue_head(&wq);
		TEST_ASSERT_EQ(
			wait_event_interruptible(&wq, wait_test_ready, &ready),
			0);
		TEST_ASSERT(
			list_empty(&current_task()->sched.wait_entry.node));
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_RUNNING);
	}
	TEST_END("sync: wait_event_interruptible ready");
	return __test_ret;
fail:
	TEST_FAIL("sync: wait_event_interruptible ready", "see above");

	return __test_ret;
}

int test_wait_event_interruptible_signal(void)
{
	struct wait_queue_head wq;
	uint64_t saved_pending = current_task()->sigctx.pending;
	uint64_t saved_blocked = current_task()->sigctx.blocked;
	bool ready = false;

	TEST_BEGIN("sync: wait_event_interruptible signal");
	{
		init_waitqueue_head(&wq);
		current_task()->sigctx.blocked &= ~signal_mask(SIGUSR1);
		TEST_ASSERT_EQ(send_current_signal(SIGUSR1), 0);
		TEST_ASSERT_EQ(
			wait_event_interruptible(&wq, wait_test_ready, &ready),
			-EINTR);
		TEST_ASSERT(
			list_empty(&current_task()->sched.wait_entry.node));
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_RUNNING);
	}
	TEST_END("sync: wait_event_interruptible signal");
	goto cleanup;
fail:
	TEST_FAIL("sync: wait_event_interruptible signal", "see above");
cleanup:
	current_task()->sigctx.pending = saved_pending;
	current_task()->sigctx.blocked = saved_blocked;

	return __test_ret;
}

int test_waitqueue_prepare_finish(void)
{
	struct wait_queue_head wq;
	struct wait_queue_entry entry;

	TEST_BEGIN("sync: waitqueue prepare finish");
	{
		init_waitqueue_head(&wq);
		init_waitqueue_entry(&entry, current_task());

		prepare_wait_entry(&wq, &entry);
		prepare_wait_entry(&wq, &entry);
		TEST_ASSERT(!list_empty(&wq.task_list));
		TEST_ASSERT(entry.wq == &wq);
		TEST_ASSERT(wq.task_list.next == &entry.node);
		TEST_ASSERT(wq.task_list.prev == &entry.node);

		finish_wait_entry(&entry);
		TEST_ASSERT(list_empty(&wq.task_list));
		TEST_ASSERT(list_empty(&entry.node));
		TEST_ASSERT(entry.wq == NULL);
	}
	TEST_END("sync: waitqueue prepare finish");
	return __test_ret;
fail:
	TEST_FAIL("sync: waitqueue prepare finish", "see above");

	return __test_ret;
}

int test_waitqueue_wake_one_fifo(void)
{
	struct wait_queue_head wq;
	struct task_struct *first = NULL;
	struct task_struct *second = NULL;

	TEST_BEGIN("sync: waitqueue wake one fifo");
	{
		first = task_alloc();
		second = task_alloc();
		TEST_ASSERT_NOT_NULL(first);
		TEST_ASSERT_NOT_NULL(second);

		init_waitqueue_head(&wq);
		prepare_wait_entry(&wq, &first->sched.wait_entry);
		prepare_wait_entry(&wq, &second->sched.wait_entry);
		task_mark_uninterruptible_sleep(first);
		task_mark_uninterruptible_sleep(second);

		TEST_ASSERT_EQ(wake_up_one(&wq), first);
		TEST_ASSERT_EQ(task_state(first), (uint32_t)TASK_RUNNING);
		TEST_ASSERT_EQ(task_state(second),
			       (uint32_t)TASK_UNINTERRUPTIBLE);
		TEST_ASSERT(list_empty(&first->sched.wait_entry.node));
		TEST_ASSERT(!list_empty(&second->sched.wait_entry.node));

		TEST_ASSERT_EQ(wake_up_one(&wq), second);
		TEST_ASSERT_EQ(task_state(second), (uint32_t)TASK_RUNNING);
		TEST_ASSERT(list_empty(&wq.task_list));
	}
	TEST_END("sync: waitqueue wake one fifo");
	goto cleanup;
fail:
	TEST_FAIL("sync: waitqueue wake one fifo", "see above");
cleanup:
	if (first) {
		if (!list_empty(&first->sched.run_list))
			sched_dequeue(first);
		finish_wait_entry(&first->sched.wait_entry);
		task_free(first);
	}
	if (second) {
		if (!list_empty(&second->sched.run_list))
			sched_dequeue(second);
		finish_wait_entry(&second->sched.wait_entry);
		task_free(second);
	}

	return __test_ret;
}

int test_waitqueue_wake_all(void)
{
	struct wait_queue_head wq;
	struct task_struct *first = NULL;
	struct task_struct *second = NULL;

	TEST_BEGIN("sync: waitqueue wake all");
	{
		first = task_alloc();
		second = task_alloc();
		TEST_ASSERT_NOT_NULL(first);
		TEST_ASSERT_NOT_NULL(second);

		init_waitqueue_head(&wq);
		prepare_wait_entry(&wq, &first->sched.wait_entry);
		prepare_wait_entry(&wq, &second->sched.wait_entry);
		task_mark_interruptible_sleep(first);
		task_mark_uninterruptible_sleep(second);

		wake_up_all(&wq);
		TEST_ASSERT_EQ(task_state(first), (uint32_t)TASK_RUNNING);
		TEST_ASSERT_EQ(task_state(second), (uint32_t)TASK_RUNNING);
		TEST_ASSERT(list_empty(&wq.task_list));
		TEST_ASSERT(list_empty(&first->sched.wait_entry.node));
		TEST_ASSERT(list_empty(&second->sched.wait_entry.node));
	}
	TEST_END("sync: waitqueue wake all");
	goto cleanup;
fail:
	TEST_FAIL("sync: waitqueue wake all", "see above");
cleanup:
	if (first) {
		if (!list_empty(&first->sched.run_list))
			sched_dequeue(first);
		finish_wait_entry(&first->sched.wait_entry);
		task_free(first);
	}
	if (second) {
		if (!list_empty(&second->sched.run_list))
			sched_dequeue(second);
		finish_wait_entry(&second->sched.wait_entry);
		task_free(second);
	}

	return __test_ret;
}

int test_wait_schedule_until_timeout(void)
{
	TEST_BEGIN("sync: wait_schedule_until timeout");
	{
		task_mark_interruptible_sleep(current_task());
		TEST_ASSERT_EQ(wait_schedule_until(TASK_INTERRUPTIBLE,
						   arch_timer_now()),
			       -ETIMEDOUT);
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_RUNNING);
		TEST_ASSERT(
			list_empty(&current_task()->sched.wait_entry.node));
	}
	TEST_END("sync: wait_schedule_until timeout");
	return __test_ret;
fail:
	TEST_FAIL("sync: wait_schedule_until timeout", "see above");

	return __test_ret;
}

int test_wait_schedule_preserves_early_wakeup(void)
{
	struct wait_queue_head wq;

	TEST_BEGIN("sync: wait_schedule preserves early wakeup");
	{
		init_waitqueue_head(&wq);
		prepare_to_wait_interruptible(&wq);
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_INTERRUPTIBLE);
		TEST_ASSERT_EQ(wake_up_one(&wq), current_task());
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_RUNNING);

		TEST_ASSERT_EQ(wait_schedule(TASK_INTERRUPTIBLE), 0);
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_RUNNING);
		TEST_ASSERT(list_empty(&wq.task_list));
		TEST_ASSERT(
			list_empty(&current_task()->sched.wait_entry.node));
	}
	TEST_END("sync: wait_schedule preserves early wakeup");
	return __test_ret;
fail:
	finish_wait(&wq);
	TEST_FAIL("sync: wait_schedule preserves early wakeup", "see above");

	return __test_ret;
}

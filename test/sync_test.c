#include <kernel/atomic.h>
#include <kernel/errno.h>
#include <kernel/exit.h>
#include <kernel/refcount.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/sync.h>
#include <kernel/task.h>
#include <kernel/test.h>
#include <kernel/timer.h>
#include <kernel/wait.h>
#include <asm/csr.h>

void test_atomic_basic(void)
{
	TEST_BEGIN("sync: atomic basic");
	{
		atomic_t value = ATOMIC_INIT(1);

		TEST_ASSERT_EQ(atomic_read(&value), 1);
		atomic_set(&value, 3);
		TEST_ASSERT_EQ(atomic_read(&value), 3);
		TEST_ASSERT_EQ(atomic_inc_return(&value), 4);
		atomic_inc(&value);
		TEST_ASSERT_EQ(atomic_read(&value), 5);
		TEST_ASSERT_EQ(atomic_dec_return(&value), 4);
		atomic_add(&value, -3);
		TEST_ASSERT_EQ(atomic_read(&value), 1);
		TEST_ASSERT(atomic_dec_and_test(&value));
		TEST_ASSERT_EQ(atomic_read(&value), 0);
		TEST_ASSERT_EQ(atomic_cmpxchg(&value, 0, 9), 0);
		TEST_ASSERT_EQ(atomic_read(&value), 9);
		TEST_ASSERT_EQ(atomic_cmpxchg(&value, 0, 1), 9);
		TEST_ASSERT_EQ(atomic_read(&value), 9);

		refcount_t refs = REFCOUNT_INIT(1);
		TEST_ASSERT_EQ(refcount_read(&refs), 1);
		refcount_inc(&refs);
		TEST_ASSERT_EQ(refcount_read(&refs), 2);
		TEST_ASSERT(!refcount_dec_and_test(&refs));
		TEST_ASSERT(refcount_dec_and_test(&refs));
		TEST_ASSERT_EQ(refcount_read(&refs), 0);
		TEST_ASSERT(!refcount_inc_not_zero(&refs));
		TEST_ASSERT(!refcount_dec_if_positive(&refs));
		refcount_inc_allow_zero(&refs);
		TEST_ASSERT_EQ(refcount_read(&refs), 1);
		TEST_ASSERT(refcount_inc_not_zero(&refs));
		TEST_ASSERT_EQ(refcount_read(&refs), 2);
		TEST_ASSERT(!refcount_dec_if_positive(&refs));
		TEST_ASSERT(refcount_dec_if_positive(&refs));
		TEST_ASSERT_EQ(refcount_read(&refs), 0);
	}
	TEST_END("sync: atomic basic");
	return;
fail:
	TEST_FAIL("sync: atomic basic", "see above");
}

void test_spinlock_irqsave(void)
{
	TEST_BEGIN("sync: spinlock irqsave");
	{
		spinlock_t lock = SPINLOCK_INIT;
		irq_flags_t flags;
		irq_flags_t before = csr_read(sstatus);

		spin_lock_irqsave(&lock, &flags);
		TEST_ASSERT(lock.locked);
		TEST_ASSERT(irqs_disabled());
		spin_unlock_irqrestore(&lock, flags);
		TEST_ASSERT(!lock.locked);
		TEST_ASSERT_EQ(csr_read(sstatus) & SSTATUS_SIE,
			       before & SSTATUS_SIE);
	}
	TEST_END("sync: spinlock irqsave");
	return;
fail:
	TEST_FAIL("sync: spinlock irqsave", "see above");
}

static bool wait_test_ready(void *arg)
{
	bool *ready = arg;

	return *ready;
}

void test_wait_event_interruptible_ready(void)
{
	struct wait_queue_head wq;
	bool ready = true;

	TEST_BEGIN("sync: wait_event_interruptible ready");
	{
		init_waitqueue_head(&wq);
		TEST_ASSERT_EQ(
			wait_event_interruptible(&wq, wait_test_ready, &ready),
			0);
			TEST_ASSERT(list_empty(&current->wait_entry.node));
		TEST_ASSERT_EQ(task_state(current), (uint32_t)TASK_RUNNING);
	}
	TEST_END("sync: wait_event_interruptible ready");
	return;
fail:
	TEST_FAIL("sync: wait_event_interruptible ready", "see above");
}

void test_wait_event_interruptible_signal(void)
{
	struct wait_queue_head wq;
	uint64_t saved_pending = current->pending;
	uint64_t saved_blocked = current->blocked;
	bool ready = false;

	TEST_BEGIN("sync: wait_event_interruptible signal");
	{
		init_waitqueue_head(&wq);
		current->blocked &= ~signal_mask(SIGUSR1);
		TEST_ASSERT_EQ(send_current_signal(SIGUSR1), 0);
		TEST_ASSERT_EQ(
			wait_event_interruptible(&wq, wait_test_ready, &ready),
			-EINTR);
			TEST_ASSERT(list_empty(&current->wait_entry.node));
		TEST_ASSERT_EQ(task_state(current), (uint32_t)TASK_RUNNING);
	}
	TEST_END("sync: wait_event_interruptible signal");
	goto cleanup;
fail:
	TEST_FAIL("sync: wait_event_interruptible signal", "see above");
cleanup:
	current->pending = saved_pending;
	current->blocked = saved_blocked;
}

void test_waitqueue_prepare_finish(void)
{
	struct wait_queue_head wq;
	struct wait_queue_entry entry;

	TEST_BEGIN("sync: waitqueue prepare finish");
	{
		init_waitqueue_head(&wq);
		init_waitqueue_entry(&entry, current);

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
	return;
fail:
	TEST_FAIL("sync: waitqueue prepare finish", "see above");
}

void test_waitqueue_wake_one_fifo(void)
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
		prepare_wait_entry(&wq, &first->wait_entry);
		prepare_wait_entry(&wq, &second->wait_entry);
		task_mark_uninterruptible_sleep(first);
		task_mark_uninterruptible_sleep(second);

		TEST_ASSERT_EQ(wake_up_one(&wq), first);
		TEST_ASSERT_EQ(task_state(first), (uint32_t)TASK_RUNNING);
		TEST_ASSERT_EQ(task_state(second),
			       (uint32_t)TASK_UNINTERRUPTIBLE);
		TEST_ASSERT(list_empty(&first->wait_entry.node));
		TEST_ASSERT(!list_empty(&second->wait_entry.node));

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
		if (!list_empty(&first->run_list))
			sched_dequeue(first);
		finish_wait_entry(&first->wait_entry);
		task_free(first);
	}
	if (second) {
		if (!list_empty(&second->run_list))
			sched_dequeue(second);
		finish_wait_entry(&second->wait_entry);
		task_free(second);
	}
}

void test_waitqueue_wake_all(void)
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
		prepare_wait_entry(&wq, &first->wait_entry);
		prepare_wait_entry(&wq, &second->wait_entry);
		task_mark_interruptible_sleep(first);
		task_mark_uninterruptible_sleep(second);

		wake_up_all(&wq);
		TEST_ASSERT_EQ(task_state(first), (uint32_t)TASK_RUNNING);
		TEST_ASSERT_EQ(task_state(second), (uint32_t)TASK_RUNNING);
		TEST_ASSERT(list_empty(&wq.task_list));
		TEST_ASSERT(list_empty(&first->wait_entry.node));
		TEST_ASSERT(list_empty(&second->wait_entry.node));
	}
	TEST_END("sync: waitqueue wake all");
	goto cleanup;
fail:
	TEST_FAIL("sync: waitqueue wake all", "see above");
cleanup:
	if (first) {
		if (!list_empty(&first->run_list))
			sched_dequeue(first);
		finish_wait_entry(&first->wait_entry);
		task_free(first);
	}
	if (second) {
		if (!list_empty(&second->run_list))
			sched_dequeue(second);
		finish_wait_entry(&second->wait_entry);
		task_free(second);
	}
}

void test_wait_schedule_until_timeout(void)
{
	TEST_BEGIN("sync: wait_schedule_until timeout");
	{
		task_mark_interruptible_sleep(current);
		TEST_ASSERT_EQ(wait_schedule_until(TASK_INTERRUPTIBLE,
						   arch_timer_now()),
			       -ETIMEDOUT);
		TEST_ASSERT_EQ(task_state(current), (uint32_t)TASK_RUNNING);
		TEST_ASSERT(list_empty(&current->wait_entry.node));
	}
	TEST_END("sync: wait_schedule_until timeout");
	return;
fail:
	TEST_FAIL("sync: wait_schedule_until timeout", "see above");
}

void test_wait_schedule_preserves_early_wakeup(void)
{
	struct wait_queue_head wq;

	TEST_BEGIN("sync: wait_schedule preserves early wakeup");
	{
		init_waitqueue_head(&wq);
		prepare_to_wait_interruptible(&wq);
		TEST_ASSERT_EQ(task_state(current), (uint32_t)TASK_INTERRUPTIBLE);
		TEST_ASSERT_EQ(wake_up_one(&wq), current);
		TEST_ASSERT_EQ(task_state(current), (uint32_t)TASK_RUNNING);

		TEST_ASSERT_EQ(wait_schedule(TASK_INTERRUPTIBLE), 0);
		TEST_ASSERT_EQ(task_state(current), (uint32_t)TASK_RUNNING);
		TEST_ASSERT(list_empty(&wq.task_list));
		TEST_ASSERT(list_empty(&current->wait_entry.node));
	}
	TEST_END("sync: wait_schedule preserves early wakeup");
	return;
fail:
	finish_wait(&wq);
	TEST_FAIL("sync: wait_schedule preserves early wakeup", "see above");
}

static mutex_t mutex_test_lock;
static volatile int mutex_test_stage;

static void mutex_waiter(void *arg)
{
	(void)arg;

	mutex_test_stage = 1;
	mutex_lock(&mutex_test_lock);
	mutex_test_stage = 2;
	mutex_unlock(&mutex_test_lock);
	do_exit(0);
}

void test_mutex_blocking(void)
{
	TEST_BEGIN("sync: mutex blocking wake");
	{
		struct task_struct *waiter;

		mutex_test_stage = 0;
		mutex_init(&mutex_test_lock);
		mutex_lock(&mutex_test_lock);
		TEST_ASSERT(!mutex_trylock(&mutex_test_lock));

		waiter = kernel_thread(mutex_waiter, NULL);
		TEST_ASSERT_NOT_NULL(waiter);

		schedule();
		TEST_ASSERT_EQ(mutex_test_stage, 1);
		TEST_ASSERT_EQ(waiter->state, (uint32_t)TASK_UNINTERRUPTIBLE);

		mutex_unlock(&mutex_test_lock);
		schedule();
		TEST_ASSERT_EQ(mutex_test_stage, 2);
		TEST_ASSERT_EQ(waiter->state, (uint32_t)TASK_ZOMBIE);

		release_task(waiter);
	}
	TEST_END("sync: mutex blocking wake");
	return;
fail:
	TEST_FAIL("sync: mutex blocking wake", "see above");
}

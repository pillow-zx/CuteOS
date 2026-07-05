/*
 * kernel/waitqueue.c - kernel wait queues
 */

#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/timer.h>
#include <kernel/tools.h>
#include <kernel/wait.h>
#include <kernel/irq.h>
#ifdef CONFIG_KERNEL_TEST
#include <kernel/test_wait.h>
#endif
#include <kernel/processor.h>

struct wait_timeout {
	struct ktimer timer;
	struct task_struct *task;
	bool fired;
};

static void wait_finish_current_state(void)
{
	if (!current_task())
		return;

	if (task_state(current_task()) & TASK_ANY_SLEEP)
		task_mark_running(current_task());
}

static bool wait_task_is_sleeping(struct task_struct *task)
{
	return (task_state(task) & TASK_ANY_SLEEP) != 0;
}

static void wait_timeout_fire(struct ktimer *timer, void *arg)
{
	struct wait_timeout *timeout =
		container_of(timer, struct wait_timeout, timer);

	(void)arg;
	timeout->fired = true;
	if (wait_task_is_sleeping(timeout->task))
		sched_wake_task(timeout->task);
}

static void wait_timeout_init(struct wait_timeout *timeout,
			      struct task_struct *task)
{
	BUG_ON(!timeout);

	ktimer_init(&timeout->timer, wait_timeout_fire, NULL);
	timeout->task = task;
	timeout->fired = false;
}

static int wait_timeout_start(struct wait_timeout *timeout, uint64_t expires)
{
	BUG_ON(!timeout);
	BUG_ON(!timeout->task);

	timeout->fired = false;
	return ktimer_arm(&timeout->timer, expires, 0);
}

static bool wait_timeout_cancel(struct wait_timeout *timeout)
{
	bool cancelled;
	bool not_fired;

	if (!timeout)
		return false;

	not_fired = !timeout->fired;
	cancelled = ktimer_cancel(&timeout->timer);
	(void)cancelled;
	return not_fired;
}

static bool wait_timeout_fired(const struct wait_timeout *timeout)
{
	return timeout && timeout->fired;
}

#ifdef CONFIG_KERNEL_TEST
static struct wait_timeout wait_timeout_test;

void wait_timeout_test_start(struct task_struct *task, uint64_t expires)
{
	if (ktimer_active(&wait_timeout_test.timer))
		(void)wait_timeout_cancel(&wait_timeout_test);

	wait_timeout_init(&wait_timeout_test, task);
	BUG_ON(wait_timeout_start(&wait_timeout_test, expires) != 0);
}

bool wait_timeout_test_cancel(void)
{
	return wait_timeout_cancel(&wait_timeout_test);
}

bool wait_timeout_test_fired(void)
{
	return wait_timeout_fired(&wait_timeout_test);
}

bool wait_timeout_test_active(void)
{
	return ktimer_active(&wait_timeout_test.timer);
}
#endif

void init_waitqueue_head(struct wait_queue_head *wq)
{
	BUG_ON(!wq);

	wq->lock.locked = 0;
	INIT_LIST_HEAD(&wq->task_list);
}

void init_waitqueue_entry(struct wait_queue_entry *entry,
			  struct task_struct *task)
{
	BUG_ON(!entry);

	INIT_LIST_HEAD(&entry->node);
	entry->task = task;
	entry->wq = NULL;
}

void prepare_wait_entry(struct wait_queue_head *wq,
			struct wait_queue_entry *entry)
{
	irq_flags_t flags;

	if (!wq || !entry)
		return;

	spin_lock_irqsave(&wq->lock, &flags);
	if (!entry->wq && list_empty(&entry->node)) {
		entry->wq = wq;
		list_add_tail(&entry->node, &wq->task_list);
	}
	spin_unlock_irqrestore(&wq->lock, flags);
}

void finish_wait_entry(struct wait_queue_entry *entry)
{
	struct wait_queue_head *wq;
	irq_flags_t flags;

	if (!entry)
		return;

	wq = entry->wq;
	if (!wq)
		return;

	spin_lock_irqsave(&wq->lock, &flags);
	if (entry->wq == wq && !list_empty(&entry->node)) {
		list_del_init(&entry->node);
		entry->wq = NULL;
	}
	spin_unlock_irqrestore(&wq->lock, flags);
}

static void prepare_to_wait(struct wait_queue_head *wq, uint32_t state)
{
	if (!wq || !current_task())
		return;

	prepare_wait_entry(wq, task_wait_entry(current_task()));
	task_set_state(current_task(), state);
}

void prepare_to_wait_uninterruptible(struct wait_queue_head *wq)
{
	prepare_to_wait(wq, TASK_UNINTERRUPTIBLE);
}

void prepare_to_wait_interruptible(struct wait_queue_head *wq)
{
	prepare_to_wait(wq, TASK_INTERRUPTIBLE);
}

void finish_wait(struct wait_queue_head *wq)
{
	(void)wq;

	if (!current_task())
		return;

	finish_wait_entry(task_wait_entry(current_task()));
	wait_finish_current_state();
}

int wait_schedule(uint32_t state)
{
	if (!current_task())
		return -EINVAL;

	if (task_state(current_task()) != state)
		return 0;
	if (!wait_task_is_sleeping(current_task()))
		task_set_state(current_task(), state);
	schedule();
	wait_finish_current_state();
	return 0;
}

int wait_schedule_until(uint32_t state, uint64_t deadline)
{
	struct wait_timeout timeout;
	bool enabled_irq_for_sleep = false;

	if (!current_task())
		return -EINVAL;
	if (deadline <= arch_timer_now()) {
		wait_finish_current_state();
		return -ETIMEDOUT;
	}

	wait_timeout_init(&timeout, current_task());
	BUG_ON(wait_timeout_start(&timeout, deadline) != 0);
	if (task_state(current_task()) != state) {
		wait_timeout_cancel(&timeout);
		wait_finish_current_state();
		return 0;
	}
	if (!wait_task_is_sleeping(current_task()))
		task_set_state(current_task(), state);

	while (!wait_timeout_fired(&timeout)) {
		if (task_state(current_task()) != state)
			break;
		if ((state == TASK_INTERRUPTIBLE) &&
		    signal_pending(current_task()))
			break;
		if (deadline <= arch_timer_now())
			break;

		if (sched_has_runnable()) {
			schedule();
			continue;
		}

		if (irqs_disabled()) {
			local_irq_enable();
			enabled_irq_for_sleep = true;
		}
		wait_for_interrupt();
	}

	if (enabled_irq_for_sleep)
		local_irq_disable();

	wait_timeout_cancel(&timeout);
	wait_finish_current_state();

	if (state == TASK_INTERRUPTIBLE && signal_pending(current_task()))
		return -EINTR;
	if (wait_timeout_fired(&timeout) || deadline <= arch_timer_now())
		return -ETIMEDOUT;
	return 0;
}

int wait_event(struct wait_queue_head *wq, wait_condition_t condition,
	       void *arg)
{
	if (!wq || !condition || !current_task())
		return -EINVAL;

	while (!condition(arg)) {
		prepare_to_wait_uninterruptible(wq);
		if (condition(arg)) {
			finish_wait(wq);
			break;
		}
		schedule();
		finish_wait(wq);
	}

	return 0;
}

int wait_event_interruptible(struct wait_queue_head *wq,
			     wait_condition_t condition, void *arg)
{
	if (!wq || !condition || !current_task())
		return -EINVAL;

	while (!condition(arg)) {
		prepare_to_wait_interruptible(wq);
		if (condition(arg)) {
			finish_wait(wq);
			break;
		}
		if (signal_pending(current_task())) {
			finish_wait(wq);
			return -EINTR;
		}
		schedule();
		finish_wait(wq);
	}

	return 0;
}

struct task_struct *wake_up_one(struct wait_queue_head *wq)
{
	struct wait_queue_entry *entry;
	struct task_struct *task = NULL;
	struct list_head *pos;
	struct list_head *next;
	irq_flags_t flags;

	if (!wq)
		return NULL;

	spin_lock_irqsave(&wq->lock, &flags);
	list_for_each_safe (pos, next, &wq->task_list) {
		entry = list_entry(pos, struct wait_queue_entry, node);
		if (!wait_task_is_sleeping(entry->task))
			continue;

		task = entry->task;
		list_del_init(&entry->node);
		entry->wq = NULL;
		break;
	}
	spin_unlock_irqrestore(&wq->lock, flags);

	if (task)
		sched_wake_task(task);
	return task;
}

void wake_up(struct wait_queue_head *wq)
{
	(void)wake_up_one(wq);
}

void wake_up_all(struct wait_queue_head *wq)
{
	while (wake_up_one(wq))
		;
}

/*
 * kernel/waitqueue.c - kernel wait queues
 */

#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/wait.h>
#ifdef CONFIG_KERNEL_TEST
#include <kernel/test_wait.h>
#endif
#include <asm/csr.h>

struct wait_timer {
	struct list_head node;
	struct task_struct *task;
	uint64_t expires;
	bool active;
	bool fired;
};

static struct {
	spinlock_t lock;
	struct list_head entries;
} wait_timer_queue = {
	.lock = SPINLOCK_INIT,
	.entries = LIST_HEAD_INIT(wait_timer_queue.entries),
};

static void wait_finish_current_state(void)
{
	if (!current)
		return;

	if (task_state(current) & TASK_ANY_SLEEP)
		task_mark_running(current);
}

static bool wait_task_is_sleeping(struct task_struct *task)
{
	return (task_state(task) & TASK_ANY_SLEEP) != 0;
}

static void wait_timer_init(struct wait_timer *timer, uint64_t expires)
{
	BUG_ON(!timer);

	INIT_LIST_HEAD(&timer->node);
	timer->task = current;
	timer->expires = expires;
	timer->active = false;
	timer->fired = false;
}

static void wait_timer_start(struct wait_timer *timer)
{
	irq_flags_t flags;

	BUG_ON(!timer);
	BUG_ON(!timer->task);

	spin_lock_irqsave(&wait_timer_queue.lock, &flags);
	BUG_ON(timer->active);
	timer->active = true;
	timer->fired = false;
	list_add_tail(&timer->node, &wait_timer_queue.entries);
	spin_unlock_irqrestore(&wait_timer_queue.lock, flags);
}

static bool wait_timer_cancel(struct wait_timer *timer)
{
	irq_flags_t flags;
	bool fired;

	if (!timer)
		return false;

	spin_lock_irqsave(&wait_timer_queue.lock, &flags);
	fired = timer->fired;
	if (timer->active) {
		list_del_init(&timer->node);
		timer->active = false;
	}
	spin_unlock_irqrestore(&wait_timer_queue.lock, flags);

	return !fired;
}

static bool wait_timer_fired(const struct wait_timer *timer)
{
	return timer && timer->fired;
}

#ifdef CONFIG_KERNEL_TEST
static struct wait_timer *
wait_timer_from_test(struct wait_timer_test_handle *handle)
{
	return (struct wait_timer *)handle;
}

void wait_timer_test_init(struct wait_timer_test_handle *handle,
			  struct task_struct *task, uint64_t expires)
{
	struct wait_timer *timer = wait_timer_from_test(handle);

	BUG_ON(sizeof(*handle) != sizeof(*timer));
	BUG_ON(offsetof(struct wait_timer_test_handle, node) !=
	       offsetof(struct wait_timer, node));
	BUG_ON(offsetof(struct wait_timer_test_handle, task) !=
	       offsetof(struct wait_timer, task));
	BUG_ON(offsetof(struct wait_timer_test_handle, expires) !=
	       offsetof(struct wait_timer, expires));
	BUG_ON(offsetof(struct wait_timer_test_handle, active) !=
	       offsetof(struct wait_timer, active));
	BUG_ON(offsetof(struct wait_timer_test_handle, fired) !=
	       offsetof(struct wait_timer, fired));

	wait_timer_init(timer, expires);
	timer->task = task;
}

void wait_timer_test_start(struct wait_timer_test_handle *handle)
{
	wait_timer_start(wait_timer_from_test(handle));
}

bool wait_timer_test_cancel(struct wait_timer_test_handle *handle)
{
	return wait_timer_cancel(wait_timer_from_test(handle));
}

bool wait_timer_test_fired(const struct wait_timer_test_handle *handle)
{
	return wait_timer_fired((const struct wait_timer *)handle);
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

void wait_timer_run_expired(uint64_t now)
{
	struct list_head *pos;
	struct list_head *next;
	irq_flags_t flags;

	spin_lock_irqsave(&wait_timer_queue.lock, &flags);
	list_for_each_safe (pos, next, &wait_timer_queue.entries) {
		struct wait_timer *timer =
			list_entry(pos, struct wait_timer, node);

		if (timer->expires > now)
			continue;

		list_del_init(&timer->node);
		timer->active = false;
		timer->fired = true;

			if (wait_task_is_sleeping(timer->task))
				sched_wake_task(timer->task);
	}
	spin_unlock_irqrestore(&wait_timer_queue.lock, flags);
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
	if (!wq || !current)
		return;

	prepare_wait_entry(wq, task_wait_entry(current));
	task_set_state(current, state);
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

	if (!current)
		return;

	finish_wait_entry(task_wait_entry(current));
	wait_finish_current_state();
}

int wait_schedule(uint32_t state)
{
	if (!current)
		return -EINVAL;

	if (task_state(current) != state)
		return 0;
	if (!wait_task_is_sleeping(current))
		task_set_state(current, state);
	schedule();
	wait_finish_current_state();
	return 0;
}

int wait_schedule_until(uint32_t state, uint64_t deadline)
{
	struct wait_timer timer;
	bool enabled_irq_for_sleep = false;

	if (!current)
		return -EINVAL;
	if (deadline <= arch_timer_now()) {
		wait_finish_current_state();
		return -ETIMEDOUT;
	}

	wait_timer_init(&timer, deadline);
	wait_timer_start(&timer);
	if (task_state(current) != state) {
		wait_timer_cancel(&timer);
		wait_finish_current_state();
		return 0;
	}
	if (!wait_task_is_sleeping(current))
		task_set_state(current, state);

	while (!wait_timer_fired(&timer)) {
		if (task_state(current) != state)
			break;
		if ((state == TASK_INTERRUPTIBLE) && signal_pending(current))
			break;
		if (deadline <= arch_timer_now())
			break;

		if (sched_has_runnable()) {
			schedule();
			continue;
		}

		if (irqs_disabled()) {
			csr_set(sstatus, SSTATUS_SIE);
			enabled_irq_for_sleep = true;
		}
		wfi();
	}

	if (enabled_irq_for_sleep)
		csr_clear(sstatus, SSTATUS_SIE);

	wait_timer_cancel(&timer);
	wait_finish_current_state();

	if (state == TASK_INTERRUPTIBLE && signal_pending(current))
		return -EINTR;
	if (wait_timer_fired(&timer) || deadline <= arch_timer_now())
		return -ETIMEDOUT;
	return 0;
}

int wait_event(struct wait_queue_head *wq, wait_condition_t condition,
	       void *arg)
{
	if (!wq || !condition || !current)
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
	if (!wq || !condition || !current)
		return -EINVAL;

	while (!condition(arg)) {
		prepare_to_wait_interruptible(wq);
		if (condition(arg)) {
			finish_wait(wq);
			break;
		}
		if (signal_pending(current)) {
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

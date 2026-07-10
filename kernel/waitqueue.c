/*
 * kernel/waitqueue.c - kernel wait queues
 */

#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/slab.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/timer.h>
#include <kernel/tools.h>
#include <kernel/wait.h>
#include <kernel/irq.h>
#include <kernel/processor.h>

struct wait_timeout {
	struct ktimer timer;
	struct task_struct *task;
};

struct wait_queue_entry {
	struct list_head node;
	struct task_struct *task;
	struct wait_queue_head *wq;
};

struct wait_registration {
	struct wait_queue_entry entry;
	struct wait_queue_head *wait_queue;
};

struct wait_registrar {
	struct task_struct *task;
	struct wait_registration *entries;
	uint32_t capacity;
	struct wait_registration inline_entry;
};

struct wait_context {
	struct wait_registrar *registrar;
	struct wait_timeout *timeout;
	bool *timeout_active;
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
	if (wait_task_is_sleeping(timeout->task))
		sched_wake_task(timeout->task);
}

static void wait_timeout_init(struct wait_timeout *timeout,
			      struct task_struct *task)
{
	BUG_ON(!timeout);

	ktimer_init(&timeout->timer, wait_timeout_fire, NULL);
	timeout->task = task;
}

static int wait_timeout_start(struct wait_timeout *timeout, uint64_t expires)
{
	BUG_ON(!timeout);
	BUG_ON(!timeout->task);

	return ktimer_arm(&timeout->timer, expires, 0);
}

static void wait_timeout_cancel(struct wait_timeout *timeout)
{
	bool cancelled;

	if (!timeout)
		return;

	cancelled = ktimer_cancel(&timeout->timer);
	(void)cancelled;
}

static void init_waitqueue_entry(struct wait_queue_entry *entry,
				 struct task_struct *task);
static void prepare_wait_entry(struct wait_queue_head *wq,
			       struct wait_queue_entry *entry);
static void finish_wait_entry(struct wait_queue_entry *entry);

static void wait_registration_init(struct wait_registration *registration,
				   struct task_struct *task)
{
	init_waitqueue_entry(&registration->entry, task);
	registration->wait_queue = NULL;
}

static int wait_registrar_init(struct wait_registrar *registrar,
			       struct task_struct *task, uint32_t capacity)
{
	uint32_t index;

	registrar->task = task;
	registrar->capacity = capacity;
	registrar->entries = &registrar->inline_entry;
	if (capacity > 1) {
		registrar->entries =
			kmalloc(sizeof(*registrar->entries) * capacity);
		if (!registrar->entries)
			return -ENOMEM;
	}

	for (index = 0; index < capacity; index++)
		wait_registration_init(&registrar->entries[index], task);
	return 0;
}

static void wait_registrar_cleanup(struct wait_registrar *registrar)
{
	uint32_t index;

	for (index = 0; index < registrar->capacity; index++)
		finish_wait_entry(&registrar->entries[index].entry);
	if (registrar->entries != &registrar->inline_entry)
		kfree(registrar->entries);
	registrar->entries = NULL;
}

static void wait_context_attach(struct wait_context *context,
				struct wait_registrar *registrar,
				struct wait_timeout *timeout,
				bool *timeout_active)
{
	struct task_struct *task = current_task();

	BUG_ON(!task);
	BUG_ON(task->active_wait);
	context->registrar = registrar;
	context->timeout = timeout;
	context->timeout_active = timeout_active;
	task->active_wait = context;
}

static void wait_context_detach(struct wait_context *context)
{
	struct task_struct *task = current_task();

	if (task && task->active_wait == context)
		task->active_wait = NULL;
}

void wait_cancel_task(struct task_struct *task)
{
	struct wait_context *context;

	if (!task)
		return;
	context = task->active_wait;
	if (!context)
		return;

	task->active_wait = NULL;
	if (*context->timeout_active) {
		wait_timeout_cancel(context->timeout);
		*context->timeout_active = false;
	}
	wait_registrar_cleanup(context->registrar);
}

int wait_register(struct wait_registrar *registrar,
		  struct wait_queue_head *wait_queue)
{
	struct wait_registration *free_registration = NULL;
	struct wait_registration *registration;
	uint32_t index;

	if (!registrar || !wait_queue)
		return -EINVAL;

	for (index = 0; index < registrar->capacity; index++) {
		registration = &registrar->entries[index];
		if (registration->wait_queue == wait_queue) {
			if (!registration->entry.wq)
				prepare_wait_entry(wait_queue,
						   &registration->entry);
			return 0;
		}
		if (!registration->wait_queue && !free_registration)
			free_registration = registration;
	}

	if (!free_registration)
		return -E2BIG;
	free_registration->wait_queue = wait_queue;
	prepare_wait_entry(wait_queue, &free_registration->entry);
	return 0;
}

static int wait_probe(const struct wait_source *source,
		      struct wait_registrar *registrar)
{
	if (!source)
		return 0;
	return source->probe(registrar, source->arg);
}

static wait_completion_t
wait_arbitrate(int probe_result, wait_flags_t flags,
	       const struct wait_deadline *deadline)
{
	if (probe_result > 0)
		return WAIT_COMPLETION_EVENT;
	if ((flags & WAIT_F_INTERRUPTIBLE) && signal_pending(current_task()))
		return WAIT_COMPLETION_SIGNAL;
	if (deadline->active && arch_timer_now() >= deadline->expires)
		return WAIT_COMPLETION_TIMEOUT;
	return 0;
}

static void wait_block_current(uint32_t sleep_state)
{
	bool enabled_irq_for_sleep = false;

	if (task_state(current_task()) != sleep_state)
		return;
	if (sched_has_runnable()) {
		schedule();
		return;
	}

	if (irqs_disabled()) {
		local_irq_enable();
		enabled_irq_for_sleep = true;
	}
	wait_for_interrupt();
	if (enabled_irq_for_sleep)
		local_irq_disable();
}

int wait_complete(const struct wait_source *source, wait_flags_t flags,
		  const struct wait_deadline *deadline,
		  wait_completion_t *completion)
{
	struct wait_registrar registrar;
	struct wait_timeout timeout;
	struct wait_context context;
	wait_completion_t result;
	uint32_t sleep_state;
	uint32_t capacity;
	bool timeout_active = false;
	int probe_result;
	int ret;

	if (!completion)
		return -EINVAL;
	*completion = 0;
	if (!deadline || !current_task())
		return -EINVAL;
	if (task_state(current_task()) != TASK_RUNNING) {
		wait_finish_current_state();
		return -EINVAL;
	}
	if (flags & ~WAIT_F_MASK)
		return -EINVAL;
	if (source &&
	    (!source->probe || source->registration_limit == 0 ||
	     source->registration_limit > WAIT_REGISTRAR_MAX_ENTRIES))
		return -EINVAL;
	if (!source && !deadline->active && !(flags & WAIT_F_INTERRUPTIBLE))
		return -EINVAL;

	capacity = source ? source->registration_limit : 1;
	ret = wait_registrar_init(&registrar, current_task(), capacity);
	if (ret < 0)
		return ret;
	wait_timeout_init(&timeout, current_task());
	wait_context_attach(&context, &registrar, &timeout, &timeout_active);

	for (;;) {
		probe_result = wait_probe(source, &registrar);
		if (probe_result < 0) {
			ret = probe_result;
			break;
		}
		result = wait_arbitrate(probe_result, flags, deadline);
		if (result) {
			*completion = result;
			ret = 0;
			break;
		}

		if (deadline->active && !timeout_active) {
			ret = wait_timeout_start(&timeout, deadline->expires);
			if (ret < 0)
				break;
			timeout_active = true;
		}

		sleep_state = (flags & WAIT_F_INTERRUPTIBLE) ?
				      TASK_INTERRUPTIBLE :
				      TASK_UNINTERRUPTIBLE;
		task_set_state(current_task(), sleep_state);

		probe_result = wait_probe(source, &registrar);
		if (probe_result < 0) {
			ret = probe_result;
			break;
		}
		result = wait_arbitrate(probe_result, flags, deadline);
		if (result) {
			*completion = result;
			ret = 0;
			break;
		}

		wait_block_current(sleep_state);
		wait_finish_current_state();
	}

	wait_context_detach(&context);
	wait_finish_current_state();
	if (timeout_active)
		wait_timeout_cancel(&timeout);
	wait_registrar_cleanup(&registrar);
	if (ret < 0)
		*completion = 0;
	return ret;
}

void init_waitqueue_head(struct wait_queue_head *wq)
{
	BUG_ON(!wq);

	wq->lock.locked = 0;
	INIT_LIST_HEAD(&wq->task_list);
}

static void init_waitqueue_entry(struct wait_queue_entry *entry,
				 struct task_struct *task)
{
	BUG_ON(!entry);

	INIT_LIST_HEAD(&entry->node);
	entry->task = task;
	entry->wq = NULL;
}

static void prepare_wait_entry(struct wait_queue_head *wq,
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

static void finish_wait_entry(struct wait_queue_entry *entry)
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

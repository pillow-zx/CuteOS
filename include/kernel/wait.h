#ifndef _CUTEOS_KERNEL_WAIT_H
#define _CUTEOS_KERNEL_WAIT_H

#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>

struct task_struct;

struct wait_queue_head {
	spinlock_t lock;
	struct list_head task_list;
};

struct wait_queue_entry {
	struct list_head node;
	struct task_struct *task;
	struct wait_queue_head *wq;
};

#define WAIT_QUEUE_HEAD_INIT(name)                                             \
	{                                                                      \
		.lock = SPINLOCK_INIT,                                         \
		.task_list = LIST_HEAD_INIT((name).task_list),                 \
	}

typedef bool (*wait_condition_t)(void *arg);

void init_waitqueue_head(struct wait_queue_head *wq);
void init_waitqueue_entry(struct wait_queue_entry *entry,
			  struct task_struct *task);
void prepare_wait_entry(struct wait_queue_head *wq,
			struct wait_queue_entry *entry);
void finish_wait_entry(struct wait_queue_entry *entry);
void prepare_to_wait_uninterruptible(struct wait_queue_head *wq);
void prepare_to_wait_interruptible(struct wait_queue_head *wq);
void finish_wait(struct wait_queue_head *wq);
void wait_timer_run_expired(uint64_t now);
int wait_schedule(uint32_t state);
int wait_schedule_until(uint32_t state, uint64_t deadline);
int wait_event(struct wait_queue_head *wq, wait_condition_t condition,
	       void *arg);
int wait_event_interruptible(struct wait_queue_head *wq,
			     wait_condition_t condition, void *arg);
struct task_struct *wake_up_one(struct wait_queue_head *wq);
/*
 * cuteOS wake_up() wakes a single waiter. Use wake_up_all() when every waiter
 * must observe the event, such as pipe endpoint shutdown.
 */
void wake_up(struct wait_queue_head *wq);
void wake_up_all(struct wait_queue_head *wq);

#endif

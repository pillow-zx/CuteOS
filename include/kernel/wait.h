#ifndef _CUTEOS_KERNEL_WAIT_H
#define _CUTEOS_KERNEL_WAIT_H

/**
 * @file wait.h
 * @brief Wait-queue primitives used by blocking kernel paths.
 */

#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>

struct task_struct;

/**
 * @struct wait_queue_head
 * @brief Head of a task wait queue.
 *
 * @par Fields
 * - @c lock: Protects task_list.
 * - @c task_list: Entries waiting on this condition.
 */
struct wait_queue_head {
	spinlock_t lock;
	struct list_head task_list;
};

/**
 * @struct wait_queue_entry
 * @brief Per-task wait-queue node.
 *
 * @par Fields
 * - @c node: Node in wait_queue_head::task_list.
 * - @c task: Sleeping task.
 * - @c wq: Queue this entry is currently linked to.
 */
struct wait_queue_entry {
	struct list_head node;
	struct task_struct *task;
	struct wait_queue_head *wq;
};

/**
 * @def WAIT_QUEUE_HEAD_INIT
 * @brief Static initializer for an empty wait queue.
 */
#define WAIT_QUEUE_HEAD_INIT(name)                                             \
	{                                                                      \
		.lock = SPINLOCK_INIT,                                         \
		.task_list = LIST_HEAD_INIT((name).task_list),                 \
	}

/**
 * @typedef wait_condition_t
 * @brief Predicate used by wait_event helpers.
 */
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
/**
 * @brief Put current task to sleep in the given TASK_* state and schedule.
 * @param state TASK_INTERRUPTIBLE or TASK_UNINTERRUPTIBLE style state.
 * @return 0 on wakeup, or a negative errno when interrupted.
 */
int wait_schedule(uint32_t state);

/**
 * @brief Sleep until wakeup, signal, or absolute deadline.
 * @param state TASK_* sleep state.
 * @param deadline Absolute mtime deadline.
 * @return 0 on wakeup, -ETIMEDOUT on timeout, or another negative errno.
 */
int wait_schedule_until(uint32_t state, uint64_t deadline);

/**
 * @brief Wait until a condition becomes true.
 * @param wq Wait queue to sleep on.
 * @param condition Predicate checked before and after sleeping.
 * @param arg Opaque predicate argument.
 * @return 0 when condition is true, or a negative errno.
 */
int wait_event(struct wait_queue_head *wq, wait_condition_t condition,
	       void *arg);
int wait_event_interruptible(struct wait_queue_head *wq,
			     wait_condition_t condition, void *arg);
struct task_struct *wake_up_one(struct wait_queue_head *wq);

void wake_up(struct wait_queue_head *wq);
void wake_up_all(struct wait_queue_head *wq);

#endif

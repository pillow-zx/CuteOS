#ifndef _CUTEOS_KERNEL_WAIT_H
#define _CUTEOS_KERNEL_WAIT_H

/**
 * @file wait.h
 * @brief Wait-queue primitives used by blocking kernel paths.
 */

#include <kernel/list.h>
#include <kernel/compiler.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>

#define WAIT_COMPLETION_EVENT	1u
#define WAIT_COMPLETION_SIGNAL	2u
#define WAIT_COMPLETION_TIMEOUT 3u

#define WAIT_F_INTERRUPTIBLE 0x01u
#define WAIT_F_MASK	     WAIT_F_INTERRUPTIBLE

#define WAIT_REGISTRAR_MAX_ENTRIES 64u

typedef uint32_t wait_completion_t;
typedef uint32_t wait_flags_t;

struct task_struct;
struct wait_context;
struct wait_registrar;
struct wait_queue_head;

struct wait_deadline {
	bool active;
	uint64_t expires;
};

/**
 * @brief Inspect or claim an event and register its wait queues.
 *
 * The probe must check or claim its event while holding the source lock and
 * call wait_register() before releasing that lock. The lock order is source
 * lock followed by wait_queue_head::lock. A positive return reports EVENT,
 * zero reports no event, and a negative return reports an operation error.
 * Edge events must be latched in source-owned state before waking a waiter.
 */
typedef int (*wait_probe_t)(struct wait_registrar *registrar, void *arg);

/**
 * @struct wait_source
 * @brief Event source observed by wait_complete().
 *
 * The source, probe context, registered wait queues, and their owning objects
 * must remain alive until wait_complete() returns. registration_limit is the
 * maximum number of distinct wait queues registered across the invocation.
 */
struct wait_source {
	wait_probe_t probe;
	void *arg;
	uint32_t registration_limit;
};

static __always_inline struct wait_deadline wait_deadline_none(void)
{
	return (struct wait_deadline){.active = false, .expires = 0};
}

static __always_inline struct wait_deadline wait_deadline_at(uint64_t expires)
{
	return (struct wait_deadline){.active = true, .expires = expires};
}

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
 * @def WAIT_QUEUE_HEAD_INIT
 * @brief Static initializer for an empty wait queue.
 */
#define WAIT_QUEUE_HEAD_INIT(name)                                             \
	{                                                                      \
		.lock = SPINLOCK_INIT,                                         \
		.task_list = LIST_HEAD_INIT((name).task_list),                 \
	}

int wait_register(struct wait_registrar *registrar,
		  struct wait_queue_head *wait_queue) __must_check
	__access_no_size(read_write, 1) __access_no_size(read_write, 2);
/**
 * @brief Wait for an event, signal, or deadline.
 *
 * Completion priority is EVENT, then SIGNAL, then TIMEOUT. Wakeups that do not
 * make any completion true are retried internally. On every return the current
 * task is running and all generic registrations and timeout state are cleaned.
 * A negative return is an operation error and leaves completion set to zero.
 */
int wait_complete(const struct wait_source *source, wait_flags_t flags,
		  const struct wait_deadline *deadline,
		  wait_completion_t *completion) __must_check
	__access_no_size(read_only, 1) __access_no_size(read_only, 3)
		__access_no_size(write_only, 4);

void wait_cancel_task(struct task_struct *task);

void init_waitqueue_head(struct wait_queue_head *wq);
struct task_struct *wake_up_one(struct wait_queue_head *wq);

void wake_up(struct wait_queue_head *wq);
void wake_up_all(struct wait_queue_head *wq);

#endif

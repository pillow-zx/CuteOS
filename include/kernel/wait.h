#ifndef _CUTEOS_KERNEL_WAIT_H
#define _CUTEOS_KERNEL_WAIT_H

/**
 * @file wait.h
 * @brief Conditional waiting primitives used by blocking kernel paths.
 */

#include <kernel/list.h>
#include <kernel/compiler.h>
#include <kernel/irq.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>

constexpr uint32_t WAIT_OUTCOME_EVENT = 1u;
constexpr uint32_t WAIT_OUTCOME_SIGNAL = 2u;
constexpr uint32_t WAIT_OUTCOME_TIMEOUT = 3u;

constexpr uint32_t WAIT_FLAG_INTERRUPTIBLE = 0x01u;
constexpr uint32_t WAIT_FLAG_KILLABLE = 0x02u;
constexpr uint32_t WAIT_FLAG_MASK =
	WAIT_FLAG_INTERRUPTIBLE | WAIT_FLAG_KILLABLE;

constexpr uint32_t WAIT_SESSION_MAX_CHANNELS = 64u;

typedef uint32_t wait_outcome_t;
typedef uint32_t wait_flags_t;

enum wait_kind {
	WAIT_KIND_GENERIC = 0,
	WAIT_KIND_MUTEX,
	WAIT_KIND_FUTEX,
	WAIT_KIND_PIPE,
	WAIT_KIND_POLL,
	WAIT_KIND_CHILD,
};

struct task_struct;
struct wait_session;
struct wait_channel;

struct wait_deadline {
	bool active;
	uint64_t expires;
};

/**
 * @brief Test whether the current context may enter wait_for().
 *
 * Waiting requires task context, a preemptible CPU, and no hard-IRQ handler.
 * This read-only guard does not change IRQ state, preempt_count, or task state.
 * wait_for() diagnoses a false result with BUG_ON before touching its inputs.
 */
static inline bool wait_context_can_sleep(void)
{
	return !in_irq() && cpu_preempt_count(current_cpu()) == 0;
}

/**
 * @brief Inspect or claim an event and register its wait channels.
 *
 * The check must inspect or claim its event while holding the source lock and
 * call wait_session_watch() before releasing that lock. The lock order is
 * source lock followed by wait_channel::lock. A positive return reports an
 * available event, zero reports no event, and a negative return reports an
 * operation error. Edge events must be latched in source-owned state before
 * waking a waiter.
 */
typedef int (*wait_check_fn)(struct wait_session *context, void *arg);

/**
 * @struct wait_request
 * @brief Adapter-owned condition observed by wait_for().
 *
 * The request, its check argument, watched channels, and their owning objects
 * must remain alive until wait_for() returns. channel_limit is the maximum
 * number of distinct channels watched during the invocation.
 */
struct wait_request {
	enum wait_kind kind;
	wait_check_fn check;
	void *arg;
	uint32_t channel_limit;
};

static inline struct wait_deadline wait_deadline_none(void)
{
	return (struct wait_deadline){.active = false, .expires = 0};
}

static inline struct wait_deadline wait_deadline_at(uint64_t expires)
{
	return (struct wait_deadline){.active = true, .expires = expires};
}

/**
 * @struct wait_channel
 * @brief Channel on which tasks wait for a possible condition change.
 *
 * @par Fields
 * - @c lock: Protects waiters.
 * - @c waiters: Tasks waiting for a possible condition change.
 */
struct wait_channel {
	spinlock_t lock;
	struct list_head waiters;
};

/**
 * @def WAIT_CHANNEL_INIT
 * @brief Static initializer for an empty wait channel.
 */
#define WAIT_CHANNEL_INIT(name)                                                \
	{                                                                      \
		.lock = SPINLOCK_INIT,                                         \
		.waiters = LIST_HEAD_INIT((name).waiters),                     \
	}

int wait_session_watch(struct wait_session *session,
		       struct wait_channel *channel) __must_check
	__access_no_size(read_write, 1) __access_no_size(read_write, 2);
/**
 * @brief Wait for an event, signal, or deadline.
 *
 * The caller must be in task context, outside hard-IRQ context, and
 * preemption-enabled. The function may block and may allocate wait-session
 * state; it preserves the caller's local IRQ state on return. It acquires
 * wait-channel locks only through the request callback and owns no caller lock.
 * The request, deadline, and outcome storage must remain valid until return.
 * Outcome priority is EVENT, then SIGNAL, then TIMEOUT.
 * `WAIT_FLAG_INTERRUPTIBLE` reports an unblocked pending signal;
 * `WAIT_FLAG_KILLABLE` reports only a pending `SIGKILL`. Wakeups that do not
 * make any outcome true are retried internally. On every return the current
 * task is running and all channel watches and timeout state are cleaned. A
 * negative return is an operation error and leaves outcome set to zero.
 * Calling this function when wait_context_can_sleep() is false is a kernel
 * context violation and triggers BUG_ON.
 */
int wait_for(const struct wait_request *request, wait_flags_t flags,
	     const struct wait_deadline *deadline,
	     wait_outcome_t *outcome) __must_check
	__access_no_size(read_only, 1) __access_no_size(read_only, 3)
		__access_no_size(write_only, 4);

void wait_cancel_task(struct task_struct *task);

void wait_channel_init(struct wait_channel *channel);
struct task_struct *wait_channel_wake_one(struct wait_channel *channel);

void wait_channel_wake_all(struct wait_channel *channel);

#endif

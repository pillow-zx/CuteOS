#ifndef _CUTEOS_KERNEL_SCHED_H
#define _CUTEOS_KERNEL_SCHED_H

/**
 * @file sched.h
 * @brief Scheduler entry points and preemption counters.
 */

#include <kernel/list.h>
#include <kernel/task.h>

/**
 * @def SCHED_MLFQ_LEVELS
 * @brief Number of runqueue levels in the single-core MLFQ scheduler.
 */
constexpr uint8_t SCHED_MLFQ_LEVELS = 4;

/**
 * @brief Initialize scheduler queues and policy state.
 */
void sched_init(void);

/**
 * @brief Switch from current task to the next runnable task.
 */
void schedule(void);

/**
 * @brief Account one timer tick and request reschedule when needed.
 */
void sched_tick(void);

/**
 * @brief Voluntarily yield the CPU from the current task.
 */
void sched_yield(void);

/**
 * @brief Initialize scheduler fields in a new task.
 * @param task Task being initialized.
 */
void sched_task_init(struct task_struct *task);

/**
 * @brief Insert a runnable task into the scheduler.
 * @param task Task in TASK_RUNNING state.
 */
void sched_enqueue(struct task_struct *task);

/**
 * @brief Make a sleeping task runnable.
 * @param task Task to wake.
 */
void sched_wakeup(struct task_struct *task);
bool sched_has_runnable(void);

/**
 * @brief Wake a specific task and enqueue it if needed.
 * @param task Task to wake.
 */
void sched_wake_task(struct task_struct *task);

/**
 * @brief Remove a task from its runqueue.
 * @param task Task that may currently be queued.
 */
void sched_dequeue(struct task_struct *task);

static inline void preempt_disable(void)
{
	cpu_inc_preempt_count(current_cpu());
}

static inline void preempt_enable(void)
{
	cpu_dec_preempt_count(current_cpu());
}

static inline bool preemptible(void)
{
	return cpu_preempt_count(current_cpu()) == 0;
}

#ifdef KERNEL_SELFTEST
bool sched_test_runqueue_empty(void);
uint32_t sched_test_runnable_count(void);
struct task_struct *sched_test_peek_next(void);
void sched_test_force_boost(void);
uint8_t sched_test_level_slice(uint8_t level);
uint8_t sched_test_level(const struct task_struct *task);
uint8_t sched_test_time_slice(const struct task_struct *task);
uint8_t sched_test_ticks(const struct task_struct *task);
uint8_t sched_test_need_resched(const struct task_struct *task);
void sched_test_set_level(struct task_struct *task, uint8_t level);
void sched_test_set_budget(struct task_struct *task, uint8_t slice,
			   uint8_t ticks);
#endif

#endif

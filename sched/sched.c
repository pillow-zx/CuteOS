/*
 * kernel/sched/core.c - 调度器核心
 */

#include "internal.h"

#include <asm/csr.h>
#include <asm/pte.h>
#include <kernel/exit.h>
#include <kernel/printk.h>

/* ---- 抢占计数器 ---- */

volatile int preempt_count;

static __always_inline uintptr_t task_satp(const struct task_struct *task)
{
	return task && task->satp ? task->satp : kernel_satp();
}

static __always_inline void switch_address_space(const struct task_struct *prev,
						 const struct task_struct *next)
{
	const uintptr_t satp_val = task_satp(next);

	if (task_satp(prev) == satp_val)
		return;

	csr_write(satp, satp_val);
	sfence_vma_all();
}

void sched_init(void)
{
	mlfq_init();
	printk("sched: MLFQ initialized (%d levels)\n", SCHED_MLFQ_LEVELS);
}

void sched_task_init(struct task_struct *task)
{
	mlfq_task_init(task);
}

void sched_enqueue(struct task_struct *task)
{
	mlfq_enqueue(task);
}

void sched_dequeue(struct task_struct *task)
{
	mlfq_dequeue(task);
}

void sched_wakeup(struct task_struct *task)
{
	mlfq_wakeup(task);
}

bool sched_has_runnable(void)
{
	return !mlfq_empty();
}

void sched_tick(void)
{
	mlfq_tick();
}

void sched_yield(void)
{
	if (!current || current == &idle_task)
		return;

	current->need_resched = 0;
	schedule();
}

void schedule(void)
{
	if (!preemptible())
		return;

	if (exited_threads_pending())
		reap_exited_threads();

	if (mlfq_empty()) {
		if (current == &idle_task || current->state == TASK_RUNNING)
			return;

		struct task_struct *prev = current;
		check_canary(prev);
		switch_address_space(prev, &idle_task);
		current = &idle_task;
		switch_to(&prev->ctx, &idle_task.ctx);
		return;
	}

	struct task_struct *next = mlfq_pick_next();
	struct task_struct *prev = current;

	if (next == prev)
		return;

	if (prev != &idle_task && prev->state == TASK_RUNNING &&
	    list_empty(&prev->run_list))
		mlfq_enqueue(prev);

	check_canary(prev);
	current = next;
	switch_address_space(prev, next);
	switch_to(&prev->ctx, &next->ctx);
}

#ifdef CONFIG_KERNEL_TEST
bool sched_test_runqueue_empty(void)
{
	return mlfq_empty();
}

uint32_t sched_test_runnable_count(void)
{
	return mlfq_count();
}

struct task_struct *sched_test_peek_next(void)
{
	return mlfq_peek_next();
}

void sched_test_force_boost(void)
{
	mlfq_boost();
}

uint8_t sched_test_level_slice(uint8_t level)
{
	return mlfq_level_slice(level);
}
#endif

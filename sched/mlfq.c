/*
 * kernel/sched/mlfq.c - 教学版多级反馈队列调度策略
 */

#include "internal.h"

#include <kernel/printk.h>
#include <kernel/timer.h>

static struct list_head queues[SCHED_MLFQ_LEVELS];
static uint8_t nonempty_bitmap;
static uint32_t runnable_count;

uint8_t mlfq_level_slice(uint8_t level)
{
	BUG_ON(level >= SCHED_MLFQ_LEVELS);
	return (uint8_t)(1U << level);
}

static void mark_nonempty(uint8_t level)
{
	nonempty_bitmap |= (uint8_t)(1U << level);
}

static void refresh_empty_mark(uint8_t level)
{
	if (list_empty(&queues[level]))
		nonempty_bitmap &= (uint8_t)~(1U << level);
}

static void reset_budget(struct task_struct *task)
{
	task->time_slice = mlfq_level_slice(task->sched_level);
	task->sched_ticks = 0;
}

static void boost_task(struct task_struct *task)
{
	task->sched_level = 0;
	reset_budget(task);
}

void mlfq_init(void)
{
	for (uint8_t i = 0; i < SCHED_MLFQ_LEVELS; i++)
		INIT_LIST_HEAD(&queues[i]);

	nonempty_bitmap = 0;
	runnable_count = 0;
}

void mlfq_task_init(struct task_struct *task)
{
	task->sched_level = 0;
	task->time_slice = mlfq_level_slice(0);
	task->sched_ticks = 0;
	task->enqueue_jiffies = 0;
}

void mlfq_enqueue(struct task_struct *task)
{
	BUG_ON(!task);
	BUG_ON(task == &idle_task);
	BUG_ON(task->sched_level >= SCHED_MLFQ_LEVELS);
	BUG_ON(!list_empty(&task->run_list));

	list_add_tail(&task->run_list, &queues[task->sched_level]);
	task->enqueue_jiffies = jiffies;
	mark_nonempty(task->sched_level);
	runnable_count++;
}

void mlfq_dequeue(struct task_struct *task)
{
	if (!task || list_empty(&task->run_list))
		return;

	uint8_t level = task->sched_level;
	BUG_ON(level >= SCHED_MLFQ_LEVELS);

	list_del_init(&task->run_list);
	refresh_empty_mark(level);
	BUG_ON(runnable_count == 0);
	runnable_count--;
}

void mlfq_wakeup(struct task_struct *task)
{
	BUG_ON(!task);
	BUG_ON(task == &idle_task);
	BUG_ON(task->sched_level >= SCHED_MLFQ_LEVELS);

	reset_budget(task);
	if (list_empty(&task->run_list))
		mlfq_enqueue(task);
}

bool mlfq_empty(void)
{
	return nonempty_bitmap == 0;
}

uint32_t mlfq_count(void)
{
	return runnable_count;
}

struct task_struct *mlfq_peek_next(void)
{
	for (uint8_t level = 0; level < SCHED_MLFQ_LEVELS; level++) {
		if ((nonempty_bitmap & (uint8_t)(1U << level)) == 0)
			continue;
		if (!list_empty(&queues[level]))
			return list_first_entry(&queues[level],
						struct task_struct, run_list);
	}

	return NULL;
}

struct task_struct *mlfq_pick_next(void)
{
	struct task_struct *next = mlfq_peek_next();
	BUG_ON(!next);
	mlfq_dequeue(next);
	return next;
}

void mlfq_boost(void)
{
	struct list_head *pos;
	struct list_head *next;

	for (uint8_t level = 0; level < SCHED_MLFQ_LEVELS; level++) {
		list_for_each_safe (pos, next, &queues[level]) {
			struct task_struct *task =
				list_entry(pos, struct task_struct, run_list);

			if (level != 0) {
				list_del_init(&task->run_list);
				refresh_empty_mark(level);
				boost_task(task);
				list_add_tail(&task->run_list, &queues[0]);
				mark_nonempty(0);
			} else {
				boost_task(task);
			}

			task->enqueue_jiffies = jiffies;
		}
	}

	if (current && current != &idle_task && current->state == TASK_RUNNING)
		boost_task(current);
}

void mlfq_tick(void)
{
	if (current && current != &idle_task &&
	    current->state == TASK_RUNNING) {
		if (current->time_slice > 0)
			current->time_slice--;
		current->sched_ticks++;

		if (current->time_slice == 0) {
			if (current->sched_level + 1 < SCHED_MLFQ_LEVELS)
				current->sched_level++;
			reset_budget(current);
			current->need_resched = 1;
		}
	}

	if (jiffies != 0 && jiffies % HZ == 0)
		mlfq_boost();
}

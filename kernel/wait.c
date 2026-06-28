/*
 * kernel/wait.c - 等待队列与睡眠原语
 */

#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/wait.h>
#include <kernel/task.h>

void init_waitqueue_head(struct wait_queue_head *wq)
{
	INIT_LIST_HEAD(&wq->task_list);
}

void wait_prepare_current_uninterruptible(void)
{
	if (current)
		task_mark_uninterruptible_sleep(current);
}

void wait_prepare_current_interruptible(void)
{
	if (current)
		task_mark_interruptible_sleep(current);
}

void wait_finish_current_state(void)
{
	if (!current)
		return;

	if (task_state(current) & TASK_ANY_SLEEP)
		task_mark_running(current);
}

bool wait_task_is_interruptible(struct task_struct *task)
{
	return task_state(task) == TASK_INTERRUPTIBLE;
}

bool wait_task_is_uninterruptible(struct task_struct *task)
{
	return task_state(task) == TASK_UNINTERRUPTIBLE;
}

bool wait_task_is_stopped(struct task_struct *task)
{
	return task_state(task) == TASK_STOPPED;
}

bool wait_task_is_sleeping(struct task_struct *task)
{
	return (task_state(task) & TASK_ANY_SLEEP) != 0;
}

void wait_wake_task(struct task_struct *task)
{
	sched_wake_task(task);
}

void prepare_to_wait_locked(struct wait_queue_head *wq)
{
	if (!wq)
		return;

	if (list_empty(&current->wait_list))
		list_add_tail(&current->wait_list, &wq->task_list);

	wait_prepare_current_uninterruptible();
}

void prepare_to_wait_interruptible(struct wait_queue_head *wq)
{
	if (!wq)
		return;

	if (list_empty(&current->wait_list))
		list_add_tail(&current->wait_list, &wq->task_list);

	wait_prepare_current_interruptible();
}

void finish_wait(struct wait_queue_head *wq)
{
	(void)wq;

	if (!current)
		return;

	if (!list_empty(&current->wait_list))
		list_del_init(&current->wait_list);

	wait_finish_current_state();
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

struct task_struct *wake_up_locked(struct wait_queue_head *wq)
{
	if (!wq || list_empty(&wq->task_list))
		return NULL;

	struct task_struct *task =
		list_first_entry(&wq->task_list, struct task_struct, wait_list);

	if (!wait_task_is_sleeping(task))
		return NULL;

	list_del_init(&task->wait_list);
	wait_wake_task(task);
	return task;
}

void sleep_on(struct wait_queue_head *wq)
{
	if (!wq)
		return;

	prepare_to_wait_locked(wq);
	schedule();
	finish_wait(wq);
}

void wake_up(struct wait_queue_head *wq)
{
	(void)wake_up_locked(wq);
}

void wake_up_all(struct wait_queue_head *wq)
{
	while (wq && !list_empty(&wq->task_list))
		wake_up(wq);
}

/*
 * kernel/wait.c - 等待队列与睡眠原语
 */

#include <kernel/wait.h>
#include <kernel/task.h>
#include <kernel/sched.h>

void init_waitqueue_head(struct wait_queue_head *wq)
{
	INIT_LIST_HEAD(&wq->task_list);
}

void prepare_to_wait_locked(struct wait_queue_head *wq)
{
	if (!wq)
		return;

	if (list_empty(&current->wait_list))
		list_add_tail(&current->wait_list, &wq->task_list);

	task_set_state(current, TASK_UNINTERRUPTIBLE);
}

void prepare_to_wait_interruptible(struct wait_queue_head *wq)
{
	if (!wq)
		return;

	if (list_empty(&current->wait_list))
		list_add_tail(&current->wait_list, &wq->task_list);

	task_set_state(current, TASK_INTERRUPTIBLE);
}

void finish_wait(struct wait_queue_head *wq)
{
	(void)wq;

	if (!current)
		return;

	if (!list_empty(&current->wait_list))
		list_del_init(&current->wait_list);

	if (task_state(current) & TASK_ANY_SLEEP)
		task_set_state(current, TASK_RUNNING);
}

struct task_struct *wake_up_locked(struct wait_queue_head *wq)
{
	if (!wq || list_empty(&wq->task_list))
		return NULL;

	struct task_struct *task =
		list_first_entry(&wq->task_list, struct task_struct, wait_list);

	if (!(task_state(task) & TASK_ANY_SLEEP))
		return NULL;

	list_del_init(&task->wait_list);
	sched_wake_task(task);
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

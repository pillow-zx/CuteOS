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

void sleep_on(struct wait_queue_head *wq)
{
	if (!wq)
		return;

	if (list_empty(&current->wait_list))
		list_add_tail(&current->wait_list, &wq->task_list);

	current->state = TASK_SLEEPING;
	schedule();

	if (!list_empty(&current->wait_list))
		list_del_init(&current->wait_list);

	current->state = TASK_RUNNING;
}

void wake_up(struct wait_queue_head *wq)
{
	if (!wq || list_empty(&wq->task_list))
		return;

	struct task_struct *task =
		list_first_entry(&wq->task_list, struct task_struct, wait_list);

	list_del_init(&task->wait_list);
	task->state = TASK_RUNNING;

	sched_wakeup(task);
}

void wake_up_all(struct wait_queue_head *wq)
{
	while (wq && !list_empty(&wq->task_list))
		wake_up(wq);
}

#ifndef _CUTEOS_KERNEL_WAIT_H
#define _CUTEOS_KERNEL_WAIT_H

/*
 * include/kernel/wait.h - 简单等待队列
 *
 * 每个 task_struct 自带一个 wait_list 节点，sleep_on 不做动态分配。
 *
 * prepare_to_wait_locked       - 不可中断睡眠（TASK_UNINTERRUPTIBLE）
 * prepare_to_wait_interruptible - 可信号打断睡眠（TASK_INTERRUPTIBLE）
 * wait_event_interruptible     - 等待条件成立，未屏蔽信号打断返回 -EINTR
 */

#include <kernel/list.h>
#include <kernel/types.h>

struct task_struct;

struct wait_queue_head {
	struct list_head task_list;
};

typedef bool (*wait_condition_t)(void *arg);

void init_waitqueue_head(struct wait_queue_head *wq);
void prepare_to_wait_locked(struct wait_queue_head *wq);
void prepare_to_wait_interruptible(struct wait_queue_head *wq);
void finish_wait(struct wait_queue_head *wq);
int wait_event_interruptible(struct wait_queue_head *wq,
			     wait_condition_t condition, void *arg);
struct task_struct *wake_up_locked(struct wait_queue_head *wq);
void sleep_on(struct wait_queue_head *wq);
void wake_up(struct wait_queue_head *wq);
void wake_up_all(struct wait_queue_head *wq);

#endif

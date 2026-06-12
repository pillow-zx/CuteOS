#ifndef _CUTEOS_KERNEL_WAIT_H
#define _CUTEOS_KERNEL_WAIT_H

/*
 * include/kernel/wait.h - 简单等待队列
 *
 * Stage 4 的等待队列只服务单核、不可被信号打断的睡眠/唤醒路径。
 * 每个 task_struct 自带一个 wait_list 节点，sleep_on 不做动态分配。
 */

#include <kernel/list.h>

struct wait_queue_head {
	struct list_head task_list;
};

void init_waitqueue_head(struct wait_queue_head *wq);
void sleep_on(struct wait_queue_head *wq);
void wake_up(struct wait_queue_head *wq);
void wake_up_all(struct wait_queue_head *wq);

#endif

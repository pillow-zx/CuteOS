/*
 * include/kernel/pid.h - PID 分配器
 *
 * 管理进程 ID（PID）的分配与回收。使用位图（bitmap）跟踪 PID 使用状态，
 * PID 范围为 0~255，提供 O(1) 的分配和释放操作。
 *
 * Functions:
 *   pid_init()    - 初始化 PID 位图，预留 PID 0 给 idle 进程
 *   alloc_pid()   - 分配一个未使用的 PID
 *   free_pid(pid) - 释放 PID 回可用池
 */

#ifndef _CUTEOS_KERNEL_PID_H
#define _CUTEOS_KERNEL_PID_H

#include <kernel/types.h>

#define PID_MAX	  255 /* 最大 PID 值 */
#define PID_COUNT 256 /* PID 总数 (0 ~ PID_MAX) */

struct task_struct;

/**
 * pid_init - 初始化 PID 位图
 *
 * 清零全部位，然后置位 PID 0（idle 进程保留）。
 */
void pid_init(void);

/**
 * alloc_pid - 分配一个可用的 PID
 *
 * 在位图中查找第一个为 0 的位，置 1 并返回对应的 PID 值。
 * 若无可用 PID 则返回 -ENOSPC。
 */
int32_t alloc_pid(void);

/**
 * free_pid - 释放一个 PID
 * @pid: 要释放的 PID 值
 */
void free_pid(pid_t pid);

void pid_attach_task(pid_t pid, struct task_struct *task);
void pid_detach_task(pid_t pid, const struct task_struct *task);
struct task_struct *pid_task(pid_t pid);
uint16_t pid_count_tasks(void);

#endif

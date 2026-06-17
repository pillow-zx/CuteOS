/*
 * kernel/pid.c - PID 分配器
 *
 * 功能：
 *   管理进程 ID（PID）的分配与回收。使用位图（bitmap）跟踪 PID 使用状态，
 *   PID 范围为 0~255，提供 O(1) 的分配和释放操作。
 *
 * 主要函数：
 *   pid_init()         - 初始化 PID 位图（清零全部位），
 *                        预留 PID 0 给 idle 进程（置位 0）。
 *   alloc_pid()        - 从位图中查找第一个为 0 的位，置 1 并返回 PID 值。
 *                        若无可用 PID 则返回错误。
 *   free_pid(pid)      - 清除位图中对应 PID 的位，释放回可用池。
 */

#include <kernel/pid.h>
#include <kernel/bitmap.h>
#include <kernel/printk.h>
#include <kernel/string.h>

BITMAP_DECLARE_STATIC(pid_map, PID_COUNT);
static struct task_struct *pid_tasks[PID_COUNT];

void pid_init(void)
{
	bitmap_zero(&pid_map);
	memset(pid_tasks, 0, sizeof(pid_tasks));

	/* 预留 PID 0 给 idle 进程 */
	bitmap_set(&pid_map, 0);

	printk("pid: bitmap initialized (%d PIDs, 0 reserved for idle)\n",
	       PID_COUNT);
}

int32_t alloc_pid(void)
{
	size_t pid = bitmap_find_first_zero(&pid_map);

	if (pid >= PID_COUNT)
		return -ENOSPC;

	bitmap_set(&pid_map, pid);
	return (int32_t)pid;
}

void free_pid(pid_t pid)
{
	if (pid == 0) {
		printk("pid: cannot free PID 0 (idle)\n");
		return;
	}

	if (pid > PID_MAX)
		return;

	pid_tasks[pid] = NULL;
	bitmap_clear(&pid_map, pid);
}

void pid_attach_task(pid_t pid, struct task_struct *task)
{
	BUG_ON(pid < 0 || pid > PID_MAX);
	BUG_ON(!task);
	BUG_ON(pid_tasks[pid] && pid_tasks[pid] != task);

	pid_tasks[pid] = task;
}

void pid_detach_task(pid_t pid, const struct task_struct *task)
{
	if (pid < 0 || pid > PID_MAX)
		return;
	if (pid_tasks[pid] == task)
		pid_tasks[pid] = NULL;
}

struct task_struct *pid_task(pid_t pid)
{
	if (pid < 0 || pid > PID_MAX)
		return NULL;

	return pid_tasks[pid];
}

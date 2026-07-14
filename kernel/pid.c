/*
 * kernel/pid.c - PID 分配器
 */

#include <kernel/pid.h>
#include <kernel/bitmap.h>
#include <kernel/errno.h>
#include <kernel/printk.h>

BITMAP_DECLARE_STATIC(pid_map, PID_COUNT);
static struct task_struct *pid_tasks[PID_COUNT];

void pid_init(void)
{
	bitmap_zero(&pid_map);
	memset(pid_tasks, 0, sizeof(pid_tasks));


	bitmap_set(&pid_map, 0);

	pr_info("pid: bitmap initialized (%zu PIDs, 0 reserved for idle)\n",
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
		pr_warn("pid: cannot free PID 0 (idle)\n");
		return;
	}

	if (pid < 0 || pid > PID_MAX)
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

uint16_t pid_count_tasks(void)
{
	uint16_t count = 0;

	for (pid_t pid = 1; pid <= PID_MAX; pid++) {
		if (pid_tasks[pid])
			count++;
	}

	return count;
}

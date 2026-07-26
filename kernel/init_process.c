/*
 * kernel/init_process.c - PID 1 init 内核线程
 */

#include <kernel/exec.h>
#include <kernel/init.h>
#include <kernel/printk.h>
#include <kernel/task.h>

void init_process(void *arg)
{
	(void)arg;
	pr_info("init running (PID %d)\n", task_pid(current_task()));

	exec_user_path("/sbin/init");
}

bool init_process_is_task(const struct task_struct *task)
{
	return task && init_task && task_tgid(task) == task_tgid(init_task);
}

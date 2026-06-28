#ifndef _CUTEOS_KERNEL_EXIT_H
#define _CUTEOS_KERNEL_EXIT_H

/*
 * include/kernel/exit.h - 进程退出与回收接口
 *
 * 声明进程退出、等待和回收核心接口，供 syscall、page_fault 和进程管理
 * 代码使用。
 */

#include <kernel/types.h>
#include <kernel/task.h>

/*
 * do_exit - 终止当前进程
 * @code: 退出码
 *
 * 释放当前进程私有资源，将 task_struct 保留为 zombie，等待父进程
 * wait4 回收。此函数不会返回。
 */
void __noreturn do_exit(int code);
void __noreturn do_exit_group(int code);
bool exited_threads_pending(void);
void reap_exited_threads(void);

struct wait4_result {
	struct task_struct *task;
	pid_t pid;
	int status;
};

/*
 * release_task - 释放已被父进程 wait 回收的 zombie task
 * @task: TASK_ZOMBIE 子进程
 */
void release_task(struct task_struct *task);

/*
 * kernel_wait4 - 等待一个可回收子进程，但不释放 task
 * @pid: pid > 0 或 pid == -1
 * @options: 当前必须为 0
 * @result: 返回 zombie task、pid 和 wait status
 */
int kernel_wait4(pid_t pid, int options, struct wait4_result *result);
void kernel_wait4_finish(struct wait4_result *result);

#endif

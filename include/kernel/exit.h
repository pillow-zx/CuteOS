#ifndef _CUTEOS_KERNEL_EXIT_H
#define _CUTEOS_KERNEL_EXIT_H

/*
 * include/kernel/exit.h - 进程退出与回收接口
 *
 * 声明 do_exit()/sys_wait4()/release_task()，供 syscall、page_fault
 * 和进程管理代码使用。
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
void do_exit(int code);
void do_exit_group(int code);
bool exited_threads_pending(void);
void reap_exited_threads(void);

/*
 * release_task - 释放已被父进程 wait 回收的 zombie task
 * @task: TASK_ZOMBIE 子进程
 */
void release_task(struct task_struct *task);

/*
 * sys_wait4 - wait4 系统调用实现
 *
 * 当前 Stage 4 支持 pid > 0 和 pid == -1，options 必须为 0。
 */
ssize_t sys_wait4(struct trap_frame *tf);

#endif

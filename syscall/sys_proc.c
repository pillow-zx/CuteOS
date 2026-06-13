/*
 * syscall/sys_proc.c - 进程相关系统调用
 *
 * 功能：
 *   实现与进程管理和信息查询相关的系统调用，为用户态提供进程控制
 *   和身份信息查询能力。
 *
 * 主要函数：
 *   sys_getpid()        - 返回当前进程 PID
 *   sys_exit(status)    - 终止当前进程
 *   sys_sched_yield()   - 主动让出 CPU
 */

#include <kernel/printk.h>
#include <kernel/syscall.h>
#include <kernel/exit.h>
#include <kernel/sched.h>
#include <kernel/task.h>
#include <asm/trap.h>

ssize_t sys_getpid(struct trap_frame *tf)
{
	(void)tf;
	return (ssize_t)current->pid;
}

ssize_t sys_getppid(struct trap_frame *tf)
{
	(void)tf;
	if (!current->parent)
		return 0;

	return (ssize_t)current->parent->pid;
}

ssize_t sys_getuid(struct trap_frame *tf)
{
	(void)tf;
	return 0;
}

ssize_t sys_getgid(struct trap_frame *tf)
{
	(void)tf;
	return 0;
}

ssize_t sys_exit(struct trap_frame *tf)
{
	int code = (int)tf->a0;
	do_exit(code);
	unreachable();
}

ssize_t sys_yield(struct trap_frame *tf)
{
	schedule();
	return 0;
}

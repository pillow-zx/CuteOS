/*
 * syscall/sys_proc.c - 进程相关系统调用
 *
 * 功能：
 *   实现与进程管理和信息查询相关的系统调用，为用户态提供进程控制
 *   和身份信息查询能力。
 *
 * 主要函数：
 *   sys_getpid()        - 返回当前进程 PID
 *   sys_getppid()       - 返回当前进程的父进程 PID
 *   sys_fork()          - 创建子进程（复制父进程地址空间和文件描述符）
 *   sys_execve(path, argv, envp) - 加载新程序替换当前进程映像
 *   sys_exit(status)    - 终止当前进程
 *   sys_wait4(pid, status, options) - 等待子进程状态变化
 *   sys_yield()         - 主动让出 CPU
 *   sys_getuid()        - 返回当前进程的用户 ID
 *   sys_getgid()        - 返回当前进程的组 ID
 */

#include <kernel/printk.h>
#include <kernel/syscall.h>
#include <asm/trap.h>

ssize_t sys_exit(struct trap_frame *tf)
{
	panic("user exit with code %ld", (long)tf->a0);
}

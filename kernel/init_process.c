/*
 * kernel/init_process.c - PID 1 init 内核线程
 *
 * init 进程是内核创建的第一个内核线程，PID 1。它由 kernel_main
 * 通过 kernel_thread() 创建。
 *
 * 当前职责：
 *   从根文件系统加载 /bin/init 并切换到用户态。
 *
 * 后续 Stage 演进：
 *   Stage 4：fork + exec + wait 循环
 *   Stage 5+：fork 出 /bin/sh，然后常驻 wait(-1) 回收孤儿进程
 *
 * 注意：init 通过 exec_user_path 切换到用户态后不再返回。
 */

#include <kernel/printk.h>
#include <kernel/syscall.h>
#include <kernel/task.h>

/**
 * init_process - PID 1 init 内核线程入口
 * @arg: 未使用
 *
 * 从根文件系统加载 /bin/init。失败时 exec_user_path 内部 panic。
 */
void init_process(void *arg)
{
	(void)arg;
	printk("init running (PID %d)\n", current->pid);

	exec_user_path("/bin/init");
}

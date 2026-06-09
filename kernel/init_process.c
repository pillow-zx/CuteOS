/*
 * kernel/init_process.c - PID 1 init 内核线程
 *
 * init 进程是内核创建的第一个内核线程，PID 1。它由 kernel_main
 * 通过 kernel_thread() 创建，永不退出。
 *
 * 职责随 Stage 演进：
 *   Stage 2：打印 "init running"，然后 while(1) yield
 *   Stage 3：execve 内嵌测试用户程序验证 U↔S 切换
 *   Stage 4：fork + exec + wait 循环
 *   Stage 5+：fork 出 /bin/sh，然后常驻 wait(-1) 回收孤儿进程
 *
 * 注意：init 是内核线程（入口为内核函数），后续通过 execve 变为用户进程。
 * init 永远不返回，其入口函数是死循环。
 */

#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/task.h>
#include <asm/csr.h>

/**
 * init_process - PID 1 init 内核线程入口
 * @arg: 未使用
 *
 * 暂时只打印 "init running" 然后 while(1) yield。
 * 后续 Stage 会扩展为 execve 用户程序或 fork shell。
 */
void init_process(void *arg)
{
	(void)arg;
	printk("init running (PID %d)\n", current->pid);

	while (1) {
		wfi();
		schedule();
	}
}

/*
 * kernel/init_process.c - PID 1 init 内核线程
 *
 * init 进程是内核创建的第一个内核线程，PID 1。它由 kernel_main
 * 通过 kernel_thread() 创建。
 *
 * 当前职责（Stage 3.2）：
 *   调用 exec_user_binary() 切换到用户态，执行内嵌的用户测试程序。
 *
 * 后续 Stage 演进：
 *   Stage 4：fork + exec + wait 循环
 *   Stage 5+：fork 出 /bin/sh，然后常驻 wait(-1) 回收孤儿进程
 *
 * 注意：init 通过 exec_user_binary 切换到用户态后不再返回。
 */

#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/task.h>
#include <asm/csr.h>

/* exec_user_elf 在 kernel/exec.c 中定义 */
extern void exec_user_elf(void *bin_start, size_t bin_size);

/* 内嵌用户 binary 符号，在 arch/riscv/user_elf.S 中定义 */
extern char _user_init_start[];
extern char _user_init_end[];

/**
 * init_process - PID 1 init 内核线程入口
 * @arg: 未使用
 *
 * 调用 exec_user_elf 加载内嵌的用户测试程序并切换到用户态。
 * 此函数在 exec_user_elf 后不再返回。
 */
void init_process(void *arg)
{
	(void)arg;
	printk("init running (PID %d)\n", current->pid);

	size_t bin_size = _user_init_end - _user_init_start;
	exec_user_elf(_user_init_start, bin_size);

	/* exec_user_elf 不返回 */
	unreachable();
}

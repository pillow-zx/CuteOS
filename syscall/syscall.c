/*
 * syscall/syscall.c - 系统调用分发
 *
 * 功能：
 *   管理系统调用分发表（syscall_table）。每个系统调用处理器接收完整的
 *   trap_frame 指针，直接从寄存器字段提取参数，无需占位符。
 *   从 tf->a7 读取系统调用号，调用 syscall_table[nr](tf)，
 *   返回值写入 tf->a0。未知的系统调用号返回 -ENOSYS。
 *
 * 主要函数：
 *   do_syscall(tf)
 *
 * 关键类型：
 *   syscall_fn_t           - typedef ssize_t (*syscall_fn_t)(struct trap_frame *)
 */

#include <kernel/syscall.h>
#include <kernel/errno.h>
#include <kernel/printk.h>
#include <kernel/task.h>
#include <kernel/mm.h>
#include <kernel/fs.h>
#include <asm/trap.h>
#include <asm/csr.h>
#include <drivers/uart.h>

#define NR_SYSCALL 1001

typedef ssize_t (*syscall_fn_t)(struct trap_frame *);

static syscall_fn_t syscall_table[NR_SYSCALL];

void do_syscall(struct trap_frame *tf)
{
	size_t nr = tf->a7;

	if (nr >= NR_SYSCALL || !syscall_table[nr]) {
		tf->a0 = (uint64_t)(-ENOSYS);
		return;
	}

	tf->a0 = (size_t)syscall_table[nr](tf);
}

void syscall_init(void)
{
	syscall_table[SYS_write] = sys_write;
	syscall_table[SYS_exit] = sys_exit;
	syscall_table[SYS_sched_yield] = sys_sched_yield;
	syscall_table[SYS_getpid] = sys_getpid;
	syscall_table[SYS_brk] = sys_brk;
	syscall_table[SYS_fork] = sys_fork;

	printk("syscall: initialized (%d entries)\n", NR_SYSCALL);
}

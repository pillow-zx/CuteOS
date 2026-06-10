/*
 * syscall/syscall.c - 系统调用分发
 *
 * 功能：
 *   管理系统调用分发表（syscall_table）。使用 typedef long
 *   (*syscall_fn_t)(uint64_t x 6) 定义统一的系统调用函数签名。从 trap_frame 的
 * a7 寄存器读取系统调用号， 以 a0~a5 作为参数调用 syscall_table[nr]。返回值写入
 *   tf->a0。 未知的系统调用号返回 -ENOSYS。当前共约 34 个系统调用。
 *
 * 主要函数：
 *   do_syscall(tf)
 *
 * 关键类型：
 *   syscall_fn_t           - typedef long (*syscall_fn_t)(uint64_t x 6)
 */

#include <kernel/syscall.h>
#include <kernel/errno.h>
#include <kernel/printk.h>
#include <kernel/task.h>
#include <kernel/fs.h>
#include <asm/trap.h>
#include <asm/csr.h>
#include <drivers/uart.h>

#define NR_SYSCALL 256

typedef ssize_t (*syscall_fn_t)(size_t, size_t, size_t, size_t, size_t, size_t);

static syscall_fn_t syscall_table[NR_SYSCALL];

void do_syscall(struct trap_frame *tf)
{
	size_t nr = tf->a7;

	if (nr >= NR_SYSCALL || !syscall_table[nr]) {
		tf->a0 = (uint64_t)(-ENOSYS);
		return;
	}

	ssize_t ret = syscall_table[nr](tf->a0, tf->a1, tf->a2, tf->a3, tf->a4,
					tf->a5);
	tf->a0 = (size_t)ret;
}

void syscall_init(void)
{
	syscall_table[SYS_write] = (syscall_fn_t)sys_write;
	syscall_table[SYS_exit] = (syscall_fn_t)sys_exit;

	printk("syscall: initialized (%d entries)\n", NR_SYSCALL);
}

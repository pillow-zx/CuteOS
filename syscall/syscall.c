/*
 * syscall/syscall.c - 系统调用分发
 *
 * 功能：
 *   管理系统调用分发表（syscall_table）。每个系统调用处理器接收完整的
 *   trap_frame 指针，通过 syscall_arg() 提取参数，无需占位符。
 *   从 syscall_nr(tf) 读取系统调用号，调用 syscall_table[nr](tf)，
 *   返回值写入 syscall 返回寄存器。未知的系统调用号返回 -ENOSYS。
 *
 * 主要函数：
 *   do_syscall(tf)
 *
 * 关键类型：
 *   syscall_fn_t           - 系统调用处理函数指针
 */

#include <kernel/syscall.h>
#include <kernel/errno.h>
#include <kernel/futex.h>
#include <kernel/printk.h>
#include <kernel/syscall_table.h>
#include <kernel/syscall_trace.h>
#include <kernel/trap.h>

typedef ssize_t (*syscall_fn_t)(struct trap_frame *);

static syscall_fn_t syscall_table[NR_SYSCALL];

void do_syscall(struct trap_frame *tf)
{
	size_t nr = syscall_nr(tf);
	size_t args[6] = {
		syscall_arg(tf, 0), syscall_arg(tf, 1), syscall_arg(tf, 2),
		syscall_arg(tf, 3), syscall_arg(tf, 4), syscall_arg(tf, 5),
	};
	ssize_t ret;

	if (nr >= NR_SYSCALL || !syscall_table[nr]) {
		ret = -ENOSYS;
		syscall_set_return(tf, ret);
		syscall_trace_log(nr, args, ret);
		return;
	}

	/*
	 * Some successful syscalls may rewrite the trap frame in place. execve
	 * installs the new user context this way; after the handler returns,
	 * keep dispatcher post-processing limited to storing the return value.
	 */
	ret = syscall_table[nr](tf);
	syscall_set_return(tf, ret);
	syscall_trace_log(nr, args, ret);
}

void syscall_init(void)
{
	futex_init();

#define INSTALL_SYSCALL(nr, name, fn) syscall_table[nr] = fn;
	SYSCALL_TABLE(INSTALL_SYSCALL)
#undef INSTALL_SYSCALL

	pr_info("syscall: initialized (%d entries)\n", NR_SYSCALL);
}

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
 *   syscall_fn_t           - 系统调用处理函数指针
 */

#include <kernel/syscall.h>
#include <kernel/errno.h>
#include <kernel/futex.h>
#include <kernel/printk.h>
#include <kernel/syscall_table.h>
#include <kernel/syscall_trace.h>
#include <asm/trap.h>

typedef ssize_t (*syscall_fn_t)(struct trap_frame *);

static syscall_fn_t syscall_table[NR_SYSCALL];

void do_syscall(struct trap_frame *tf)
{
	size_t nr = tf->a7;
	size_t args[6] = {tf->a0, tf->a1, tf->a2, tf->a3, tf->a4, tf->a5};
	ssize_t ret;

	if (nr >= NR_SYSCALL || !syscall_table[nr]) {
		ret = -ENOSYS;
		tf->a0 = (uint64_t)ret;
		syscall_trace_log(nr, args, ret);
		return;
	}

	/*
	 * Some successful syscalls may rewrite the trap frame in place. execve
	 * installs the new user context this way; after the handler returns,
	 * keep dispatcher post-processing limited to storing the return value.
	 */
	ret = syscall_table[nr](tf);
	tf->a0 = (size_t)ret;
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

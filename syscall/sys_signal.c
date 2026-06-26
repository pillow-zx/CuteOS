/*
 * syscall/sys_signal.c - 信号相关系统调用（ABI 边界层）
 *
 * 功能：
 *   实现信号相关的系统调用。本文件属于 ABI 边界层：负责从 trap_frame
 *   解包参数，并委托给 kernel/signal.c 中的内部 do_* 函数。
 *
 *   kill 在目标进程中设置 pending 位。sigaction 对 SIGKILL/SIGSTOP
 *   返回 -EINVAL（不可捕获）。sigprocmask 设置信号阻塞掩码。
 *   sigreturn 从信号栈恢复被中断的上下文和 blocked 掩码。
 *
 * 主要函数：
 *   sys_kill(pid, sig)               - 设置目标进程的 pending 位
 *   sys_sigaction(sig, act, oldact)  - 注册信号处理器（SIGKILL/SIGSTOP
 *                                      返回 -EINVAL）
 *   sys_sigprocmask(how, set, oldset)- 设置/查询信号阻塞掩码
 *   sys_sigreturn()                  - 恢复信号帧和 blocked 掩码
 */

#include <kernel/errno.h>
#include <kernel/signal.h>
#include <kernel/syscall.h>
#include <kernel/types.h>
#include <asm/trap.h>

ssize_t sys_kill(struct trap_frame *tf)
{
	return do_kill((pid_t)tf->a0, (int)tf->a1);
}

ssize_t sys_tgkill(struct trap_frame *tf)
{
	return do_tgkill((pid_t)tf->a0, (pid_t)tf->a1, (int)tf->a2);
}

ssize_t sys_sigaltstack(struct trap_frame *tf)
{
	(void)tf;
	return do_sigaltstack();
}

ssize_t sys_sigaction(struct trap_frame *tf)
{
	int sig = (int)tf->a0;
	const struct sigaction *act = (const struct sigaction *)tf->a1;
	struct sigaction *oldact = (struct sigaction *)tf->a2;
	size_t sigsetsize = (size_t)tf->a3;

	return do_sigaction(sig, act, oldact, sigsetsize);
}

ssize_t sys_sigprocmask(struct trap_frame *tf)
{
	int how = (int)tf->a0;
	const uint64_t *set = (const uint64_t *)tf->a1;
	uint64_t *oldset = (uint64_t *)tf->a2;
	size_t sigsetsize = (size_t)tf->a3;

	return do_sigprocmask(how, set, oldset, sigsetsize);
}

ssize_t sys_sigreturn(struct trap_frame *tf)
{
	return do_sigreturn(tf, tf->sp);
}

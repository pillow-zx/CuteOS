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
#include <kernel/mm.h>
#include <kernel/signal.h>
#include <kernel/syscall.h>
#include <kernel/types.h>
#include <asm/trap.h>

ssize_t sys_kill(struct trap_frame *tf)
{
	long pid = (long)tf->a0;
	int sig = (int)tf->a1;

	if (pid <= 0)
		return -EINVAL;

	return do_kill(pid, sig);
}

ssize_t sys_tkill(struct trap_frame *tf)
{
	long tid = (long)tf->a0;
	int sig = (int)tf->a1;

	if (tid <= 0)
		return -EINVAL;

	return do_tkill(tid, sig);
}

ssize_t sys_tgkill(struct trap_frame *tf)
{
	long tgid = (long)tf->a0;
	long tid = (long)tf->a1;
	int sig = (int)tf->a2;

	if (tgid <= 0 || tid <= 0)
		return -EINVAL;

	return do_tgkill(tgid, tid, sig);
}

ssize_t sys_sigaltstack(struct trap_frame *tf)
{
	const struct stack_t *ss = (const struct stack_t *)tf->a0;
	struct stack_t *old_ss = (struct stack_t *)tf->a1;
	struct stack_t kss;
	struct stack_t old;
	int ret;

	if (old_ss) {
		ret = do_sigaltstack(NULL, &old);
		if (ret < 0)
			return ret;
		if (copy_to_user(old_ss, &old, sizeof(old)) != 0)
			return -EFAULT;
	}

	if (!ss)
		return 0;

	if (copy_from_user(&kss, ss, sizeof(kss)) != 0)
		return -EFAULT;
	return do_sigaltstack(&kss, NULL);
}

ssize_t sys_sigaction(struct trap_frame *tf)
{
	int sig = (int)tf->a0;
	const struct sigaction *act = (const struct sigaction *)tf->a1;
	struct sigaction *oldact = (struct sigaction *)tf->a2;
	size_t sigsetsize = (size_t)tf->a3;
	struct sigaction kact;
	struct sigaction old;
	int ret;

	if (!signal_is_valid(sig))
		return -EINVAL;
	if (!signal_is_catchable(sig) && act)
		return -EINVAL;
	if (sigsetsize != sizeof(unsigned long))
		return -EINVAL;

	if (oldact) {
		ret = do_sigaction(sig, NULL, &old);
		if (ret < 0)
			return ret;
		if (copy_to_user(oldact, &old, sizeof(old)) != 0)
			return -EFAULT;
	}

	if (!act)
		return 0;

	if (copy_from_user(&kact, act, sizeof(kact)) != 0)
		return -EFAULT;
	return do_sigaction(sig, &kact, NULL);
}

ssize_t sys_sigprocmask(struct trap_frame *tf)
{
	int how = (int)tf->a0;
	const uint64_t *set = (const uint64_t *)tf->a1;
	uint64_t *oldset = (uint64_t *)tf->a2;
	size_t sigsetsize = (size_t)tf->a3;
	uint64_t newset;
	uint64_t old;
	int ret;

	if (sigsetsize != sizeof(unsigned long))
		return -EINVAL;

	if (oldset) {
		ret = do_sigprocmask(how, NULL, &old);
		if (ret < 0)
			return ret;
		if (copy_to_user(oldset, &old, sizeof(old)) != 0)
			return -EFAULT;
	}

	if (!set)
		return 0;

	if (copy_from_user(&newset, set, sizeof(newset)) != 0)
		return -EFAULT;
	return do_sigprocmask(how, &newset, NULL);
}

ssize_t sys_sigreturn(struct trap_frame *tf)
{
	uintptr_t sp = (uintptr_t)tf->sp;

	return do_sigreturn(tf, sp);
}

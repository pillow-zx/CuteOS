/*
 * syscall/sys_signal.c - 信号相关系统调用（ABI 边界层）
 */

#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/signal.h>
#include <kernel/syscall.h>
#include <kernel/types.h>
#include <kernel/trap.h>

/*
 * SYSCALL_SUPPORT(B): kill
 * Current: delivers to a positive pid.
 * Unsupported errno: pid <= 0 returns -EINVAL; missing target returns -ESRCH.
 * Future: add pid 0, -1, process-group, and permission semantics.
 */
ssize_t sys_kill(struct trap_frame *tf)
{
	long pid = (long)syscall_arg(tf, 0);
	int sig = (int)syscall_arg(tf, 1);

	if (pid <= 0)
		return -EINVAL;

	return do_kill(pid, sig);
}

/*
 * SYSCALL_SUPPORT(B): tkill
 * Current: delivers to a positive tid.
 * Unsupported errno: tid <= 0 returns -EINVAL; missing target returns -ESRCH.
 * Future: add credential checks with the permission model.
 */
ssize_t sys_tkill(struct trap_frame *tf)
{
	long tid = (long)syscall_arg(tf, 0);
	int sig = (int)syscall_arg(tf, 1);

	if (tid <= 0)
		return -EINVAL;

	return do_tkill(tid, sig);
}

/*
 * SYSCALL_SUPPORT(B): tgkill
 * Current: delivers to a positive tgid/tid pair.
 * Unsupported errno: non-positive ids return -EINVAL; missing or mismatched
 * target returns -ESRCH.
 * Future: add credential checks with tkill.
 */
ssize_t sys_tgkill(struct trap_frame *tf)
{
	long tgid = (long)syscall_arg(tf, 0);
	long tid = (long)syscall_arg(tf, 1);
	int sig = (int)syscall_arg(tf, 2);

	if (tgid <= 0 || tid <= 0)
		return -EINVAL;

	return do_tgkill(tgid, tid, sig);
}

/*
 * SYSCALL_SUPPORT(B): sigaltstack
 * Current: registers, disables, or queries one alternate signal stack.
 * Unsupported errno: changing while on-stack returns -EPERM; unknown ss_flags
 * return -EINVAL; too-small stacks return -ENOMEM.
 * Future: document flag policy such as SS_AUTODISARM.
 */
ssize_t sys_sigaltstack(struct trap_frame *tf)
{
	const struct stack_t *ss = (const struct stack_t *)syscall_arg(tf, 0);
	struct stack_t *old_ss = (struct stack_t *)syscall_arg(tf, 1);
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

/*
 * SYSCALL_SUPPORT(B): rt_sigaction
 * Current: installs/query handlers and masks with a fixed unsigned long sigset.
 * Unsupported errno: bad sigset size, uncatchable signals with act, or SIG_ERR
 * handler returns -EINVAL; SA_* semantics are shallow.
 * Future: build a signal action flag matrix.
 */
ssize_t sys_sigaction(struct trap_frame *tf)
{
	int sig = (int)syscall_arg(tf, 0);
	const struct sigaction *act = (const struct sigaction *)syscall_arg(tf, 1);
	struct sigaction *oldact = (struct sigaction *)syscall_arg(tf, 2);
	size_t sigsetsize = (size_t)syscall_arg(tf, 3);
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

/*
 * SYSCALL_SUPPORT(B): rt_sigprocmask
 * Current: updates/query blocked mask with an unsigned long sigset ABI.
 * Unsupported errno: bad sigset size or invalid how value returns -EINVAL.
 * Future: keep ABI size assertions and expand signal-mask edge coverage.
 */
ssize_t sys_sigprocmask(struct trap_frame *tf)
{
	int how = (int)syscall_arg(tf, 0);
	const uint64_t *set = (const uint64_t *)syscall_arg(tf, 1);
	uint64_t *oldset = (uint64_t *)syscall_arg(tf, 2);
	size_t sigsetsize = (size_t)syscall_arg(tf, 3);
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

/*
 * SYSCALL_SUPPORT(B): rt_sigreturn
 * Current: restores the current kernel-tracked user signal frame. Nested
 * handlers return in LIFO order; unmatched or invalid frames terminate with
 * SIGSEGV.
 * Future: continue architecture-state and restart-safety coverage.
 */
ssize_t sys_sigreturn(struct trap_frame *tf)
{
	uintptr_t sp = trap_user_sp(tf);

	return do_sigreturn(tf, sp);
}

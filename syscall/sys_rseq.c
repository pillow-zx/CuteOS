/*
 * syscall/sys_rseq.c - restartable sequence syscall ABI wrapper
 */

#include <kernel/rseq.h>
#include <kernel/syscall.h>
#include <kernel/trap.h>

/*
 * SYSCALL_SUPPORT(B): rseq
 * Current: supports single-core register, unregister, resume, and abort paths.
 * Unsupported errno: unknown flags return -EINVAL; signature mismatch returns
 * -EPERM; duplicate matching registration returns -EBUSY.
 * Future: define flag, migration, and mm_cid policy before SMP work.
 */
ssize_t sys_rseq(struct trap_frame *tf)
{
	struct rseq *area = (struct rseq *)syscall_arg(tf, 0);
	uint32_t len = (uint32_t)syscall_arg(tf, 1);
	int flags = (int)syscall_arg(tf, 2);
	uint32_t sig = (uint32_t)syscall_arg(tf, 3);

	return kernel_rseq(area, len, flags, sig);
}

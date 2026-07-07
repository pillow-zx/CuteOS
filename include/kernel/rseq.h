#ifndef _CUTEOS_KERNEL_RSEQ_H
#define _CUTEOS_KERNEL_RSEQ_H

/**
 * @file rseq.h
 * @brief Restartable sequences task state and user-return hooks.
 */

#include <kernel/compiler.h>
#include <kernel/types.h>

struct rseq;
struct task_struct;
struct trap_frame;

/**
 * @struct task_rseq_context
 * @brief Per-task rseq registration cached in task_struct.
 *
 * @par Fields
 * - @c area: Registered userspace rseq area, or NULL.
 * - @c len: Userspace-provided rseq area length.
 * - @c sig: Signature used to validate unregister requests.
 * - @c need_update: Userspace cpu_id fields need refresh.
 */
struct task_rseq_context {
	struct rseq *area;
	uint32_t len;
	uint32_t sig;
	uint8_t need_update;
};

/**
 * @brief Implement the Linux rseq syscall for the current task.
 * @param area Userspace rseq area, or registered area for unregister.
 * @param len Length supplied by userspace.
 * @param flags Linux rseq flags.
 * @param sig Userspace signature value.
 * @return 0 on success, or a negative errno.
 */
ssize_t __must_check kernel_rseq(struct rseq *area, uint32_t len, int flags,
				 uint32_t sig);

/**
 * @brief Clear rseq state across execve.
 * @param task Task that just installed a new program image.
 */
void __nonnull(1) rseq_execve(struct task_struct *task);

/**
 * @brief Copy or clear rseq registration for clone according to flags.
 * @param child New task.
 * @param parent Source task.
 * @param flags Linux clone flags.
 */
void __nonnull(1, 2) rseq_clone(struct task_struct *child,
				const struct task_struct *parent,
				unsigned long flags);

/**
 * @brief Abort active rseq critical section on preemption when required.
 * @param prev Task being switched out.
 */
void rseq_sched_switch(struct task_struct *prev);

/**
 * @brief Refresh rseq userspace fields before returning to user mode.
 * @param tf Trap frame for the returning user context.
 * @return 0 on success, or a negative errno converted to signal/fault path.
 */
int __must_check __nonnull(1) rseq_resume_user(struct trap_frame *tf);

/**
 * @brief Abort an active rseq critical section before signal delivery.
 * @param tf Trap frame that signal delivery will rewrite.
 * @return 0 on success, or a negative errno.
 */
int __must_check __nonnull(1) rseq_signal_deliver(struct trap_frame *tf);

#endif

#ifndef _CUTEOS_KERNEL_FORK_H
#define _CUTEOS_KERNEL_FORK_H

/**
 * @file fork.h
 * @brief clone/fork task creation staging API.
 */

#include <kernel/types.h>
#include <kernel/task.h>

struct trap_frame;

/**
 * @struct kernel_clone
 * @brief Prepared clone operation staged between allocation and publication.
 *
 * @par Fields
 * - @c task: Child task under construction.
 * - @c flags: Linux clone flags.
 * - @c pid: Allocated child TID.
 */
struct kernel_clone {
	struct task_struct *task;
	unsigned long flags;
	pid_t pid;
};

/**
 * @brief Allocate and initialize a child task without publishing it.
 * @param tf Parent user trap frame.
 * @param flags Linux clone flags.
 * @param child_stack Optional userspace child stack pointer.
 * @param tls Optional TLS value.
 * @param clear_child_tid Optional userspace futex clear address.
 * @param clone Output staging object.
 * @return 0 on success, or a negative errno.
 */
int kernel_clone_prepare(struct trap_frame *tf, unsigned long flags,
			 uintptr_t child_stack, uintptr_t tls,
			 int *clear_child_tid, struct kernel_clone *clone);

/**
 * @brief Publish a prepared clone as runnable.
 * @param clone Prepared clone object.
 * @return Child TID on success, or a negative errno.
 */
pid_t kernel_clone_commit(struct kernel_clone *clone);

/**
 * @brief Roll back a prepared clone that was not committed.
 * @param clone Prepared clone object.
 */
void kernel_clone_abort(struct kernel_clone *clone);

/**
 * @brief Implement syscall-level clone from a parent trap frame.
 * @param tf Parent user trap frame.
 * @param flags Linux clone flags.
 * @param child_stack Optional userspace child stack pointer.
 * @param parent_tid Optional userspace parent_tid pointer.
 * @param tls Optional TLS value.
 * @param child_tid Optional userspace child_tid pointer.
 * @return Child TID in parent, or a negative errno.
 */
ssize_t kernel_clone_from_frame(struct trap_frame *tf, unsigned long flags,
				uintptr_t child_stack, int *parent_tid,
				uintptr_t tls, int *child_tid);

#endif

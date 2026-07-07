#ifndef _CUTEOS_KERNEL_FUTEX_H
#define _CUTEOS_KERNEL_FUTEX_H

/**
 * @file futex.h
 * @brief Futex wait/wake and robust-list kernel entry points.
 */

#include <kernel/types.h>
#include <kernel/task.h>
#include <uapi/futex.h>

/**
 * @struct futex_deadline
 * @brief Optional absolute timeout for FUTEX_WAIT.
 *
 * @par Fields
 * - @c active: Whether @ref expires is meaningful.
 * - @c expires: Absolute mtime deadline.
 */
struct futex_deadline {
	bool active;
	uint64_t expires;
};

void futex_init(void);

/**
 * @brief Wake tasks waiting on a futex word in one address space.
 * @param mm Address space containing @p uaddr.
 * @param uaddr Userspace futex word.
 * @param nr Maximum number of waiters to wake.
 * @return Number of tasks woken.
 */
int futex_wake_mm(struct mm_struct *mm, int *uaddr, int nr);

/**
 * @brief Process a task's robust futex list during exit.
 * @param task Exiting task.
 */
void futex_exit_robust_list(struct task_struct *task);

/**
 * @brief Implement supported Linux futex operations for the current task.
 * @param uaddr Userspace futex word.
 * @param op Linux futex operation plus private flag bits.
 * @param val Operation-specific integer argument.
 * @param deadline Optional wait deadline.
 * @return Operation result or a negative errno.
 */
int kernel_futex(int *uaddr, int op, int val,
		 const struct futex_deadline *deadline);
int futex_set_robust_list(struct task_struct *task,
			  struct robust_list_head *head, size_t len);
int futex_get_robust_list(struct task_struct *task,
			  struct robust_list_head **head, size_t *len);

#endif

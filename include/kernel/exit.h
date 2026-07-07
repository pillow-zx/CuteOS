#ifndef _CUTEOS_KERNEL_EXIT_H
#define _CUTEOS_KERNEL_EXIT_H

/**
 * @file exit.h
 * @brief 进程退出与 wait4 回收接口。
 */

#include <kernel/types.h>
#include <kernel/task.h>

/**
 * @brief Terminate the current task with an encoded exit status.
 * @param code Wait-visible exit status.
 */
void __noreturn do_exit(int code);

/**
 * @brief Terminate every task in the current thread group.
 * @param code Wait-visible exit status.
 */
void __noreturn do_exit_group(int code);
bool exited_threads_pending(void);
void reap_exited_threads(void);

/**
 * @struct wait4_result
 * @brief Deferred wait4 result whose task release is finished by caller.
 *
 * @par Fields
 * - @c task: Reaped task pending release.
 * - @c cputime: CPU time charged to the child.
 * - @c pid: Wait result pid.
 * - @c status: Linux wait status.
 */
struct wait4_result {
	struct task_struct *task;
	struct task_cputime cputime;
	pid_t pid;
	int status;
};

/**
 * @brief Release a zombie task after it is no longer waitable.
 * @param task Zombie task to release.
 */
void release_task(struct task_struct *task);

/**
 * @brief Wait for a child process according to Linux wait4 pid/options.
 * @param pid pid selector accepted by the current implementation.
 * @param options Linux wait options.
 * @param result Output result; finalized by kernel_wait4_finish().
 * @return Child pid, 0 for WNOHANG no-child-ready, or a negative errno.
 */
int kernel_wait4(pid_t pid, int options, struct wait4_result *result);

/**
 * @brief Finish a successful wait4 by releasing held task state.
 * @param result Result previously filled by kernel_wait4().
 */
void kernel_wait4_finish(struct wait4_result *result);

#endif

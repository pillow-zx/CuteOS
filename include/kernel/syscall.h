#ifndef _CUTEOS_KERNEL_SYSCALL_H
#define _CUTEOS_KERNEL_SYSCALL_H

/**
 * @file syscall.h
 * @brief Syscall handler declarations and dispatch API.
 */

#include <kernel/trap.h>
#include <kernel/syscall_table.h>
#include <kernel/types.h>
#include <uapi/mman.h>
#include <uapi/sched.h>

struct task_struct;

/**
 * @def DECLARE_SYSCALL
 * @brief Expand syscall-table entries into handler declarations.
 */
#define DECLARE_SYSCALL(nr, name, fn) ssize_t fn(struct trap_frame *);
SYSCALL_TABLE(DECLARE_SYSCALL)
#undef DECLARE_SYSCALL

/**
 * @brief Dispatch one Linux riscv64 syscall from a trap frame.
 * @param tf Trap frame containing a7 syscall number and a0-a5 arguments.
 *
 * The dispatcher writes the handler result back to a0. Handlers return
 * non-negative success values or negative Linux errno values.
 */
void do_syscall(struct trap_frame *tf);

/**
 * @brief Consume a pending interrupted syscall for a signal handler return.
 * @param task Task whose syscall context is considered.
 * @param tf User trap frame to restore to the original ecall.
 * @param sa_restart Whether the selected signal action has SA_RESTART.
 * @return True when @p tf now restarts the original syscall.
 *
 * This is the single restart-policy seam shared by the dispatcher and signal
 * delivery.  It currently covers read, write, wait4, and timeout-free
 * FUTEX_WAIT only.
 */
bool restart_for_signal(struct task_struct *task,
				struct trap_frame *tf, bool sa_restart);

/**
 * @brief Discard any task-local interrupted syscall context.
 * @param task Task to clear, or NULL.
 */
void restart_clear(struct task_struct *task);

/**
 * @brief Initialize the syscall dispatch table from SYSCALL_TABLE.
 */
void syscall_init(void);

#endif

#ifndef _CUTEOS_KERNEL_SYSCALL_H
#define _CUTEOS_KERNEL_SYSCALL_H

/*
 * include/kernel/syscall.h - syscall handler declarations and dispatch API
 *
 * Handler prototypes are generated from SYSCALL_TABLE so syscall metadata
 * stays in one place: <kernel/syscall_table.h>.
 */

#include <kernel/trap.h>
#include <kernel/syscall_table.h>
#include <kernel/types.h>
#include <uapi/mman.h>
#include <uapi/sched.h>

#define DECLARE_SYSCALL(nr, name, fn) ssize_t fn(struct trap_frame *);
SYSCALL_TABLE(DECLARE_SYSCALL)
#undef DECLARE_SYSCALL

void do_syscall(struct trap_frame *tf);
void syscall_init(void);

#endif

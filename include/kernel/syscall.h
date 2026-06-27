#ifndef _CUTEOS_KERNEL_SYSCALL_H
#define _CUTEOS_KERNEL_SYSCALL_H

/*
 * include/kernel/syscall.h - syscall handler declarations and dispatch API
 *
 * Handler prototypes are generated from SYSCALL_TABLE so syscall metadata
 * stays in one place: <kernel/syscall_table.h>.
 */

#include <asm/csr.h>
#include <kernel/syscall_table.h>
#include <uapi/mman.h>
#include <uapi/sched.h>

struct trap_frame;

#define DECLARE_SYSCALL(nr, name, fn) ssize_t fn(struct trap_frame *);
SYSCALL_TABLE(DECLARE_SYSCALL)
#undef DECLARE_SYSCALL

static __always_inline bool user_access_begin(void)
{
	bool had_sum = (csr_read(sstatus) & SSTATUS_SUM) != 0;
	if (!had_sum)
		csr_set(sstatus, SSTATUS_SUM);
	return had_sum;
}

static __always_inline void user_access_end(bool had_sum)
{
	if (!had_sum)
		csr_clear(sstatus, SSTATUS_SUM);
}

void do_syscall(struct trap_frame *tf);
void syscall_init(void);

#endif

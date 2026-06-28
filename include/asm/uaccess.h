#ifndef _CUTEOS_ASM_UACCESS_H
#define _CUTEOS_ASM_UACCESS_H

/*
 * include/asm/uaccess.h - architecture user access primitives
 *
 * These helpers only manage the RISC-V SUM bit.  Range validation and
 * copy/fault-in policy live in mm/uaccess.c.
 */

#include <asm/csr.h>
#include <kernel/compiler.h>
#include <kernel/types.h>

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

#endif /* _CUTEOS_ASM_UACCESS_H */

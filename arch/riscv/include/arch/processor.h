#ifndef _CUTEOS_ARCH_RISCV_PROCESSOR_H
#define _CUTEOS_ARCH_RISCV_PROCESSOR_H

#include <kernel/compiler.h>
#include <kernel/types.h>
#include <asm/csr.h>

static __always_inline void wait_for_interrupt(void)
{
	wfi();
}

static __always_inline __must_check uintptr_t trap_pc(void)
{
	return csr_read(sepc);
}

static __always_inline __must_check uintptr_t trap_cause(void)
{
	return csr_read(scause);
}

static __always_inline __must_check uintptr_t trap_value(void)
{
	return csr_read(stval);
}

static __always_inline void flush_icache(void)
{
	icache_flush();
}

#endif

#ifndef _CUTEOS_ASM_CONTEXT_H
#define _CUTEOS_ASM_CONTEXT_H

/*
 * arch/riscv/include/asm/context.h - switch.S saved context layout
 */

#include <kernel/types.h>

struct context {
	size_t ra;
	size_t sp;
	size_t s0, s1, s2, s3, s4, s5;
	size_t s6, s7, s8, s9, s10, s11;
};

#endif

#ifndef _CUTEOS_ARCH_RISCV_SYSTEM_H
#define _CUTEOS_ARCH_RISCV_SYSTEM_H

#include <kernel/compiler.h>

enum system_reset_mode {
	ARCH_SYSTEM_RESET_RESTART,
	ARCH_SYSTEM_RESET_HALT,
	ARCH_SYSTEM_RESET_POWER_OFF,
};

void __noreturn system_reset(enum system_reset_mode mode);

#endif

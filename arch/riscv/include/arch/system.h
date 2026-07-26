#ifndef _CUTEOS_ARCH_RISCV_SYSTEM_H
#define _CUTEOS_ARCH_RISCV_SYSTEM_H

#include <kernel/compiler.h>

enum arch_system_reset_mode {
	ARCH_SYSTEM_RESET_RESTART,
	ARCH_SYSTEM_RESET_HALT,
	ARCH_SYSTEM_RESET_POWER_OFF,
};

void __noreturn arch_system_reset(enum arch_system_reset_mode mode);

#endif

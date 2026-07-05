#ifndef _CUTEOS_ARCH_RISCV_SBI_H
#define _CUTEOS_ARCH_RISCV_SBI_H

#include <kernel/compiler.h>

void sbi_console_putchar(int ch);
void __noreturn sbi_shutdown(void);

#endif

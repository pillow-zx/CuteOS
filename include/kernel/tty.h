#ifndef _CUTEOS_KERNEL_TTY_H
#define _CUTEOS_KERNEL_TTY_H

#include <kernel/types.h>

/*
 * include/kernel/tty.h - UART-backed single-console TTY adapter
 */

void tty_console_init(void);
int tty_console_start(void) __must_check;

#endif

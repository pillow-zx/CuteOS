#ifndef _CUTEOS_KERNEL_TTY_H
#define _CUTEOS_KERNEL_TTY_H

#include <kernel/types.h>

/*
 * include/kernel/tty.h - 最小 TTY/session 策略 helper
 */

struct task_struct;

void tty_console_init_session(struct task_struct *task);
int tty_console_acquire(int steal);
int tty_console_release(void);
int tty_console_get_foreground_pgid(pid_t *pgid);
int tty_console_set_foreground_pgid(pid_t pgid);
int tty_console_get_sid(pid_t *sid);
int tty_deliver_signal(int sig);

#endif

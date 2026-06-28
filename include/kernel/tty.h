#ifndef _CUTEOS_KERNEL_TTY_H
#define _CUTEOS_KERNEL_TTY_H

/*
 * include/kernel/tty.h - 最小 TTY/session 策略 helper
 *
 * console 驱动保留 termios 行规程和设备 I/O。本层负责在 cuteOS 当前
 * 简化前台任务模型下投递终端产生的信号。
 */

int tty_deliver_signal(int sig);

#endif

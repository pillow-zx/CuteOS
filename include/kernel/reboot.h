#ifndef _CUTEOS_KERNEL_REBOOT_H
#define _CUTEOS_KERNEL_REBOOT_H

#include <kernel/compiler.h>

enum kernel_reboot_command {
	KERNEL_REBOOT_CAD_OFF,
	KERNEL_REBOOT_CAD_ON,
	KERNEL_REBOOT_RESTART,
	KERNEL_REBOOT_HALT,
	KERNEL_REBOOT_POWER_OFF,
};

int kernel_reboot(enum kernel_reboot_command command) __must_check;

#endif

#ifndef _CUTEOS_KERNEL_KSYMS_H
#define _CUTEOS_KERNEL_KSYMS_H

#include <kernel/types.h>

struct ksym {
	uintptr_t addr;
	const char *name;
};

const char *ksym_lookup(uintptr_t addr, uintptr_t *offset);

#endif

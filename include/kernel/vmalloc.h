#ifndef _CUTEOS_KERNEL_VMALLOC_H
#define _CUTEOS_KERNEL_VMALLOC_H

#include <kernel/compiler.h>
#include <kernel/types.h>

void vmalloc_init(void);
void *__must_check __malloc __alloc_size(1) vmalloc(size_t size);
void __nonnull(1) vfree(void *ptr);

#endif

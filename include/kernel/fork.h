#ifndef _CUTEOS_KERNEL_FORK_H
#define _CUTEOS_KERNEL_FORK_H

#include <kernel/types.h>
#include <asm/trap.h>

ssize_t kernel_clone_from_frame(struct trap_frame *tf, unsigned long flags,
				uintptr_t child_stack, int *parent_tid,
				uintptr_t tls, int *child_tid);

#endif /* _CUTEOS_KERNEL_FORK_H */

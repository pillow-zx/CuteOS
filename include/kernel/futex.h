#ifndef _CUTEOS_KERNEL_FUTEX_H
#define _CUTEOS_KERNEL_FUTEX_H

#include <kernel/types.h>

#define FUTEX_WAIT	    0
#define FUTEX_WAKE	    1
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CMD_MASK	    0x7f

struct mm_struct;
struct trap_frame;

void futex_init(void);
int futex_wake_mm(struct mm_struct *mm, int *uaddr, int nr);
ssize_t sys_futex(struct trap_frame *tf);

#endif

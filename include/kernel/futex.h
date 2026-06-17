#ifndef _CUTEOS_KERNEL_FUTEX_H
#define _CUTEOS_KERNEL_FUTEX_H

#include <kernel/types.h>

#define FUTEX_WAIT	    0
#define FUTEX_WAKE	    1
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CMD_MASK	    0x7f
#define FUTEX_WAITERS	    0x80000000U
#define FUTEX_OWNER_DIED    0x40000000U
#define FUTEX_TID_MASK	    0x3fffffffU

struct mm_struct;
struct task_struct;
struct trap_frame;

struct robust_list {
	struct robust_list *next;
};

struct robust_list_head {
	struct robust_list list;
	long futex_offset;
	struct robust_list *list_op_pending;
};

void futex_init(void);
int futex_wake_mm(struct mm_struct *mm, int *uaddr, int nr);
void futex_exit_robust_list(struct task_struct *task);
ssize_t sys_futex(struct trap_frame *tf);

#endif

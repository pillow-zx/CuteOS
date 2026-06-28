#ifndef _CUTEOS_KERNEL_FUTEX_H
#define _CUTEOS_KERNEL_FUTEX_H

#include <kernel/types.h>
#include <kernel/task.h>

#define FUTEX_WAIT	       0
#define FUTEX_WAKE	       1
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CMD_MASK	   0x7f
#define FUTEX_WAITERS	   0x80000000U
#define FUTEX_OWNER_DIED   0x40000000U
#define FUTEX_TID_MASK	   0x3fffffffU

struct futex_deadline {
	bool active;
	uint64_t expires;
};

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
int kernel_futex(int *uaddr, int op, int val,
		 const struct futex_deadline *deadline);
int futex_set_robust_list(struct task_struct *task,
			  struct robust_list_head *head, size_t len);
int futex_get_robust_list(struct task_struct *task,
			  struct robust_list_head **head, size_t *len);

#endif

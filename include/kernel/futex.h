#ifndef _CUTEOS_KERNEL_FUTEX_H
#define _CUTEOS_KERNEL_FUTEX_H

#include <kernel/types.h>
#include <kernel/task.h>
#include <uapi/futex.h>

struct futex_deadline {
	bool active;
	uint64_t expires;
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

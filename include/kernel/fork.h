#ifndef _CUTEOS_KERNEL_FORK_H
#define _CUTEOS_KERNEL_FORK_H

#include <kernel/types.h>
#include <kernel/task.h>
#include <asm/trap.h>

struct kernel_clone {
	struct task_struct *task;
	unsigned long flags;
	pid_t pid;
};

int kernel_clone_prepare(struct trap_frame *tf, unsigned long flags,
			 uintptr_t child_stack, uintptr_t tls,
			 int *clear_child_tid, struct kernel_clone *clone);
pid_t kernel_clone_commit(struct kernel_clone *clone);
void kernel_clone_abort(struct kernel_clone *clone);
ssize_t kernel_clone_from_frame(struct trap_frame *tf, unsigned long flags,
				uintptr_t child_stack, int *parent_tid,
				uintptr_t tls, int *child_tid);

#endif /* _CUTEOS_KERNEL_FORK_H */

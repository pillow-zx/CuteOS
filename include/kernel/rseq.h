#ifndef _CUTEOS_KERNEL_RSEQ_H
#define _CUTEOS_KERNEL_RSEQ_H

#include <kernel/compiler.h>
#include <kernel/types.h>

struct rseq;
struct task_struct;
struct trap_frame;

struct task_rseq_context {
	struct rseq *area;
	uint32_t len;
	uint32_t sig;
	uint8_t need_update;
};

ssize_t __must_check kernel_rseq(struct rseq *area, uint32_t len, int flags,
				 uint32_t sig);
void __nonnull(1) rseq_execve(struct task_struct *task);
void __nonnull(1, 2) rseq_clone(struct task_struct *child,
				const struct task_struct *parent,
				unsigned long flags);
void rseq_sched_switch(struct task_struct *prev);
int __must_check __nonnull(1) rseq_resume_user(struct trap_frame *tf);
int __must_check __nonnull(1) rseq_signal_deliver(struct trap_frame *tf);

#endif

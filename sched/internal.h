#ifndef _CUTEOS_KERNEL_SCHED_INTERNAL_H
#define _CUTEOS_KERNEL_SCHED_INTERNAL_H

#include <kernel/sched.h>

void mlfq_init(void);
void mlfq_task_init(struct task_struct *task);
void mlfq_enqueue(struct task_struct *task);
void mlfq_dequeue(struct task_struct *task);
void mlfq_wakeup(struct task_struct *task);
void mlfq_tick(void);
void mlfq_boost(void);
bool mlfq_empty(void);
uint32_t mlfq_count(void);
struct task_struct *mlfq_pick_next(void);
struct task_struct *mlfq_peek_next(void);
uint8_t mlfq_level_slice(uint8_t level);

#endif

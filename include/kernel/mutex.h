#ifndef _CUTEOS_KERNEL_SYNC_H
#define _CUTEOS_KERNEL_SYNC_H

#include <kernel/compiler.h>
#include <kernel/printk.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>
#include <kernel/wait.h>

struct task_struct;

typedef struct {
	spinlock_t lock;
	struct task_struct *owner;
	struct wait_channel wait;
} mutex_t;

#define MUTEX_INIT(name)                                                       \
	{                                                                      \
		.lock = SPINLOCK_INIT,                                         \
		.owner = NULL,                                                 \
		.wait = WAIT_CHANNEL_INIT((name).wait),                        \
	}
#define DEFINE_MUTEX(name) mutex_t name = MUTEX_INIT(name)

void mutex_init(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
bool mutex_trylock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);

#endif

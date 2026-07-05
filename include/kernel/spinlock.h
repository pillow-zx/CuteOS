#ifndef _CUTEOS_KERNEL_SPINLOCK_H
#define _CUTEOS_KERNEL_SPINLOCK_H

#include <kernel/compiler.h>
#include <kernel/irq.h>
#include <kernel/printk.h>
#include <kernel/types.h>

typedef struct {
	volatile int locked;
} spinlock_t;

#define SPINLOCK_INIT	      {.locked = 0}
#define DEFINE_SPINLOCK(name) spinlock_t name = SPINLOCK_INIT

static __always_inline void spin_lock_irqsave(spinlock_t *lock,
					      irq_flags_t *flags)
{
	BUG_ON(!lock);
	BUG_ON(!flags);

	*flags = local_irq_save();
	BUG_ON(lock->locked);
	lock->locked = 1;
	asm volatile("" ::: "memory");
}

static __always_inline void spin_unlock_irqrestore(spinlock_t *lock,
						   irq_flags_t flags)
{
	BUG_ON(!lock);
	BUG_ON(!lock->locked);

	asm volatile("" ::: "memory");
	lock->locked = 0;
	local_irq_restore(flags);
}

#endif

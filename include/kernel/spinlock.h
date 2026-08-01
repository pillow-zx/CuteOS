#ifndef _CUTEOS_KERNEL_SPINLOCK_H
#define _CUTEOS_KERNEL_SPINLOCK_H

#include <kernel/atomic.h>
#include <kernel/compiler.h>
#include <kernel/irq.h>
#include <kernel/printk.h>
#include <kernel/types.h>

typedef struct {
	atomic_t locked;
} spinlock_t;

#define SPINLOCK_INIT	      {.locked = ATOMIC_INIT(0)}
#define DEFINE_SPINLOCK(name) spinlock_t name = SPINLOCK_INIT

__always_inline
static inline void spin_lock_init(spinlock_t *lock)
{
	BUG_ON(!lock);
	atomic_set(&lock->locked, 0);
}

__always_inline
static inline void spin_lock_irqsave(spinlock_t *lock, irq_flags_t *flags)
{
	int expected;

	BUG_ON(!lock);
	BUG_ON(!flags);

	*flags = local_irq_save();
	do {
		expected = 0;
	} while (!atomic_try_cmpxchg_acquire(&lock->locked, &expected, 1));
}

__always_inline
static inline void spin_unlock_irqrestore(spinlock_t *lock, irq_flags_t flags)
{
	int old;

	BUG_ON(!lock);

	old = atomic_xchg_release(&lock->locked, 0);
	BUG_ON(old != 1);
	local_irq_restore(flags);
}

#endif

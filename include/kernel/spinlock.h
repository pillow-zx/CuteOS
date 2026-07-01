#ifndef _CUTEOS_KERNEL_SPINLOCK_H
#define _CUTEOS_KERNEL_SPINLOCK_H

#include <asm/csr.h>
#include <kernel/compiler.h>
#include <kernel/printk.h>
#include <kernel/types.h>

typedef unsigned long irq_flags_t;

typedef struct {
	volatile int locked;
} spinlock_t;

#define SPINLOCK_INIT	      {.locked = 0}
#define DEFINE_SPINLOCK(name) spinlock_t name = SPINLOCK_INIT

static __always_inline irq_flags_t local_irq_save(void)
{
	irq_flags_t flags = csr_read(sstatus);

	csr_clear(sstatus, SSTATUS_SIE);
	return flags;
}

static __always_inline void local_irq_restore(irq_flags_t flags)
{
	if (flags & SSTATUS_SIE)
		csr_set(sstatus, SSTATUS_SIE);
	else
		csr_clear(sstatus, SSTATUS_SIE);
}

static __always_inline bool irqs_disabled(void)
{
	return (csr_read(sstatus) & SSTATUS_SIE) == 0;
}

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

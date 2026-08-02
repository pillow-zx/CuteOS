#ifndef _CUTEOS_KERNEL_SPINLOCK_H
#define _CUTEOS_KERNEL_SPINLOCK_H

#include <kernel/atomic.h>
#include <kernel/compiler.h>
#include <kernel/irq.h>
#include <kernel/printk.h>
#include <kernel/types.h>

typedef struct spinlock {
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

/**
 * @brief Test whether the current CPU owns at least one spinlock.
 *
 * The query is available in every build and never allocates or changes
 * context state. Debug builds additionally make individual lock membership
 * queryable through spinlock_is_held_by_current().
 */
__always_inline __must_check __pure
static inline bool spinlock_held(void)
{
	return lock_depth() != 0;
}

/**
 * @brief Test whether a lock is recorded as held by the current CPU.
 *
 * The address set is a debug-only diagnostic. In a non-debug build there is
 * no membership information, so callers must use spinlock_held() instead.
 */
__always_inline __must_check __pure __nonnull(1)
static inline bool spinlock_held_by_current(const spinlock_t *lock)
{
#ifdef CONFIG_DEBUG_CONTEXT
	const struct cpu *cpu = current_cpu();

	for (uint32_t index = 0; index < cpu_lock_depth(cpu); index++)
		if (cpu->locks[index] == lock)
			return true;
#else
	(void)lock;
#endif
	return false;
}

__always_inline __nonnull(1)
static inline void spinlock_track_acquire(spinlock_t *lock,
					  irq_flags_t irq_flags)
{
	struct cpu *cpu = current_cpu();
	uint32_t depth = cpu_lock_depth(cpu);

	if (depth == 0)
		cpu->lock_irq_flags = irq_flags;

#ifdef CONFIG_DEBUG_CONTEXT
	BUG_ON(depth >= CPU_LOCK_MAX);
	cpu->locks[depth] = lock;
#else
	(void)lock;
#endif
	cpu->lock_depth++;
}

__always_inline __nonnull(1)
static inline void spinlock_track_release(const spinlock_t *lock)
{
	struct cpu *cpu = current_cpu();
	uint32_t depth = cpu_lock_depth(cpu);

#ifdef CONFIG_DEBUG_CONTEXT
	uint32_t found = depth;

	for (uint32_t index = 0; index < depth; index++) {
		if (cpu->locks[index] == lock) {
			found = index;
			break;
		}
	}

	BUG_ON(found == depth);
	for (uint32_t index = found + 1; index < depth; index++)
		cpu->locks[index - 1] = cpu->locks[index];
	cpu->locks[depth - 1] = NULL;
#else
	(void)lock;
#endif
	BUG_ON(depth == 0);
	cpu->lock_depth = depth - 1;
}

__always_inline
static inline void spin_lock_irqsave(spinlock_t *lock, irq_flags_t *flags)
{
	int expected;

	BUG_ON(!lock);
	BUG_ON(!flags);

#ifdef CONFIG_DEBUG_CONTEXT
	BUG_ON(spinlock_held_by_current(lock));
	BUG_ON(lock_depth() >= CPU_LOCK_MAX);
#endif

	*flags = local_irq_save();
	do {
		expected = 0;
	} while (!atomic_try_cmpxchg_acquire(&lock->locked, &expected, 1));
	spinlock_track_acquire(lock, *flags);
}

__always_inline
static inline void spin_unlock_irqrestore(spinlock_t *lock, irq_flags_t flags)
{
	int old;
	struct cpu *cpu = current_cpu();

	BUG_ON(!lock);
	/*
	 * Unlock order is intentionally non-LIFO. Restore the IRQ state saved
	 * by the outermost acquire only after the last held lock is released;
	 * restoring this lock's flags here could re-enable IRQs too early.
	 */
	(void)flags;

#ifdef CONFIG_DEBUG_CONTEXT
	BUG_ON(!spinlock_held_by_current(lock));
#endif

	old = atomic_xchg_release(&lock->locked, 0);
	BUG_ON(old != 1);
	spinlock_track_release(lock);
	if (cpu_lock_depth(cpu) == 0)
		local_irq_restore(cpu->lock_irq_flags);
}

#endif

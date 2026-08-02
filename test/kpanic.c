#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/spinlock.h>
#include <kernel/test.h>

#ifdef KERNEL_PANIC_TEST

#if !defined(KPANIC_CASE_PREEMPT_UNDERFLOW) && \
	!defined(KPANIC_CASE_PREEMPT_OVERFLOW) && \
	!defined(KPANIC_CASE_SPINLOCK_WRONG_UNLOCK) && \
	!defined(KPANIC_CASE_SPINLOCK_RECURSIVE) && \
	!defined(KPANIC_CASE_SPINLOCK_CAPACITY)
#error "KERNEL_PANIC_TEST requires a valid KERNEL_PANIC_CASE"
#endif

__noreturn
void kernel_panic_test_run(void)
{
#ifdef KPANIC_CASE_PREEMPT_UNDERFLOW
	pr_info("[KPANIC] case=preempt-underflow\n");
	preempt_enable();
#elif defined(KPANIC_CASE_PREEMPT_OVERFLOW)
	pr_info("[KPANIC] case=preempt-overflow\n");
	cpu_set_preempt_count(current_cpu(), INT32_MAX);
	preempt_disable();
#elif defined(KPANIC_CASE_SPINLOCK_WRONG_UNLOCK)
	spinlock_t held = SPINLOCK_INIT;
	spinlock_t other = SPINLOCK_INIT;
	irq_flags_t flags;

	pr_info("[KPANIC] case=spinlock-wrong-unlock\n");
	spin_lock_init(&held);
	spin_lock_init(&other);
	spin_lock_irqsave(&held, &flags);
	spin_unlock_irqrestore(&other, flags);
#elif defined(KPANIC_CASE_SPINLOCK_RECURSIVE)
	spinlock_t lock = SPINLOCK_INIT;
	irq_flags_t flags;

	pr_info("[KPANIC] case=spinlock-recursive\n");
	spin_lock_init(&lock);
	spin_lock_irqsave(&lock, &flags);
	spin_lock_irqsave(&lock, &flags);
#elif defined(KPANIC_CASE_SPINLOCK_CAPACITY)
	spinlock_t locks[CPU_LOCK_MAX + 1] = {0};
	irq_flags_t flags[CPU_LOCK_MAX + 1];

	pr_info("[KPANIC] case=spinlock-capacity\n");
	for (uint32_t index = 0; index < CPU_LOCK_MAX + 1; index++)
		spin_lock_init(&locks[index]);
	for (uint32_t index = 0; index < CPU_LOCK_MAX; index++)
		spin_lock_irqsave(&locks[index], &flags[index]);
	spin_lock_irqsave(&locks[CPU_LOCK_MAX],
			 &flags[CPU_LOCK_MAX]);
#endif

	panic("kpanic case returned without triggering its assertion\n");
}

#endif

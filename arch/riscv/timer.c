/*
 * arch/riscv/timer.c - Sstc 时钟 (100Hz)
 */

#include <kernel/timer.h>
#include <kernel/errno.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/time.h>

volatile uint64_t jiffies = 0;

uint64_t arch_timer_now(void)
{
	return csr_read(time);
}

void arch_timer_set(uint64_t value)
{
	csr_write(stimecmp, value);
}

void timer_run_expired(uint64_t now)
{
	ktimer_run_expired(now);
}

int timer_sleep_until(uint64_t expires, bool interruptible)
{
	if (!current_task())
		return -EINVAL;
	if (interruptible && signal_pending(current_task()))
		return -EINTR;
	if (expires <= arch_timer_now())
		return 0;

	if (interruptible) {
		task_mark_interruptible_sleep(current_task());
		return wait_schedule_until(TASK_INTERRUPTIBLE, expires);
	}
	task_mark_uninterruptible_sleep(current_task());
	return wait_schedule_until(TASK_UNINTERRUPTIBLE, expires);
}

void arch_timer_init(void)
{
	arch_timer_set(arch_timer_now() + CLOCKS_PER_TICK);
}

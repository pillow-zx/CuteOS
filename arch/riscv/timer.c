/*
 * arch/riscv/timer.c - Sstc 时钟 (100Hz)
 */

#include <kernel/timer.h>
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

void arch_timer_init(void)
{
	arch_timer_set(arch_timer_now() + CLOCKS_PER_TICK);
}

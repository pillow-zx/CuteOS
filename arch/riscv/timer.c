/*
 * arch/riscv/timer.c - Sstc 时钟 (100Hz)
 */

#include <kernel/timer.h>
#include <kernel/time.h>

volatile uint64_t jiffies = 0;

uint64_t timer_now(void)
{
	return csr_read(time);
}

void timer_set(uint64_t value)
{
	csr_write(stimecmp, value);
}

void timer_run_expired(uint64_t now)
{
	ktimer_run_expired(now);
}

void timer_init(void)
{
	timer_set(timer_now() + CLOCKS_PER_TICK);
}

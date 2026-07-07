/**
 * @file timer.h
 * @brief 时钟 tick、mtime 和睡眠接口。
 */

#ifndef _CUTEOS_KERNEL_TIMER_H
#define _CUTEOS_KERNEL_TIMER_H

#include <kernel/types.h>
#include <kernel/list.h>

/**
 * @def HZ
 * @brief Scheduler/accounting ticks per second.
 */
#define HZ 100UL

/**
 * @def MTIME_FREQ
 * @brief QEMU virt mtime frequency in ticks per second.
 */
#define MTIME_FREQ 10000000UL

/**
 * @def CLOCKS_PER_TICK
 * @brief Number of mtime ticks in one scheduler tick.
 */
#define CLOCKS_PER_TICK (MTIME_FREQ / HZ)

/**
 * @brief Global scheduler tick count.
 */
extern volatile uint64_t jiffies;

/**
 * @brief Read the architecture mtime counter.
 * @return Current mtime ticks.
 */
uint64_t arch_timer_now(void);

/**
 * @brief Program the next architecture timer interrupt.
 * @param value Absolute mtime tick value.
 */
void arch_timer_set(uint64_t value);

/**
 * @brief Run expired kernel timers.
 * @param now Current mtime tick value.
 */
void timer_run_expired(uint64_t now);

/**
 * @brief Sleep current task until an absolute mtime deadline.
 * @param expires Absolute mtime tick value.
 * @param interruptible Whether pending signals may interrupt the sleep.
 * @return 0 on timeout, or a negative errno when interrupted.
 */
int timer_sleep_until(uint64_t expires, bool interruptible);

/**
 * @brief Initialize architecture timer hardware.
 */
void arch_timer_init(void);

#endif

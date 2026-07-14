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
constexpr uint64_t HZ = 100ULL;

/**
 * @def MTIME_FREQ
 * @brief QEMU virt mtime frequency in ticks per second.
 */
constexpr uint64_t MTIME_FREQ = 10000000ULL;

/**
 * @def CLOCKS_PER_TICK
 * @brief Number of mtime ticks in one scheduler tick.
 */
constexpr uint64_t CLOCKS_PER_TICK = MTIME_FREQ / HZ;

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
 * @brief Initialize architecture timer hardware.
 */
void arch_timer_init(void);

#endif

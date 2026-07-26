/**
 * @file pid.h
 * @brief PID allocator and pid-to-task lookup table.
 */

#ifndef _CUTEOS_KERNEL_PID_H
#define _CUTEOS_KERNEL_PID_H

#include <kernel/types.h>

/**
 * @def PID_MAX
 * @brief Highest allocatable PID/TID in the current teaching kernel.
 */
constexpr pid_t PID_MAX = 255;

/**
 * @def PID_COUNT
 * @brief Number of entries in the PID lookup table, including PID 0.
 */
constexpr size_t PID_COUNT = 256;

/**
 * @brief Initialize PID allocator state.
 */
void pid_init(void);

/**
 * @brief Allocate a free positive PID/TID.
 * @return PID on success, or a negative errno.
 */
int32_t __must_check alloc_pid(void);

/**
 * @brief Release a PID allocated by alloc_pid().
 * @param pid PID/TID to free.
 */
void free_pid(pid_t pid);

uint16_t __must_check pid_count_tasks(void);

#endif

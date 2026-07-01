#ifndef _CUTEOS_KERNEL_TIME_H
#define _CUTEOS_KERNEL_TIME_H

#include <kernel/types.h>
#include <uapi/time.h>

bool clock_id_supported(int clock_id);
void mtime_to_timespec(uint64_t ticks, struct timespec *ts);
int timespec_to_mtime_delta(const struct timespec *ts, uint64_t *delta);
uint64_t mtime_deadline_after(uint64_t now, uint64_t delta);

#endif

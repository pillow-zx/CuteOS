/*
 * kernel/worker.c - small kernel worker helpers
 */

#include <kernel/time.h>
#include <kernel/timer.h>
#include <kernel/wait.h>
#include <kernel/worker.h>

static uint64_t worker_interval_ticks(unsigned int interval_sec)
{
	return (uint64_t)interval_sec * MTIME_FREQ;
}

void worker_run_periodic(unsigned int interval_sec, void (*work)(void *),
			 void *arg)
{
	uint64_t interval;

	if (!work || interval_sec == 0)
		return;

	interval = worker_interval_ticks(interval_sec);
	for (;;) {
		struct wait_deadline deadline = wait_deadline_at(
			mtime_deadline_after(arch_timer_now(), interval));
		wait_completion_t completion;
		int ret;

		ret = wait_complete(NULL, 0, &deadline, &completion);
		if (ret < 0 || completion != WAIT_COMPLETION_TIMEOUT)
			return;
		work(arg);
	}
}

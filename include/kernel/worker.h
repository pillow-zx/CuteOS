#ifndef _CUTEOS_KERNEL_WORKER_H
#define _CUTEOS_KERNEL_WORKER_H

/*
 * include/kernel/worker.h - small kernel worker helpers
 *
 * These helpers are intentionally thin wrappers around existing kernel-thread
 * and timer primitives.  They centralize common background-loop mechanics
 * without introducing an async I/O subsystem or worker pool.
 */

void worker_run_periodic(unsigned int interval_sec, void (*work)(void *),
			 void *arg);

#endif

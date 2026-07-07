#ifndef _CUTEOS_KERNEL_WORKER_H
#define _CUTEOS_KERNEL_WORKER_H

/*
 * include/kernel/worker.h - small kernel worker helpers
 */

void worker_run_periodic(unsigned int interval_sec, void (*work)(void *),
			 void *arg);

#endif

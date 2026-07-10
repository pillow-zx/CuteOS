#ifndef _CUTEOS_KERNEL_TEST_WAIT_H
#define _CUTEOS_KERNEL_TEST_WAIT_H

#ifdef KERNEL_SELFTEST

#include <kernel/types.h>

struct task_struct;

void wait_timeout_test_start(struct task_struct *task, uint64_t expires);
bool wait_timeout_test_cancel(void);
bool wait_timeout_test_fired(void);
bool wait_timeout_test_active(void);

#endif

#endif

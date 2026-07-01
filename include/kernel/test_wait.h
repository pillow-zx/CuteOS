#ifndef _CUTEOS_KERNEL_TEST_WAIT_H
#define _CUTEOS_KERNEL_TEST_WAIT_H

#ifdef CONFIG_KERNEL_TEST

#include <kernel/list.h>
#include <kernel/types.h>

struct task_struct;

struct wait_timer_test_handle {
	struct list_head node;
	struct task_struct *task;
	uint64_t expires;
	bool active;
	bool fired;
};

void wait_timer_test_init(struct wait_timer_test_handle *timer,
			  struct task_struct *task, uint64_t expires);
void wait_timer_test_start(struct wait_timer_test_handle *timer);
bool wait_timer_test_cancel(struct wait_timer_test_handle *timer);
bool wait_timer_test_fired(const struct wait_timer_test_handle *timer);

#endif

#endif

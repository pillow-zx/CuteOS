#ifndef _CUTEOS_KERNEL_USER_RETURN_H
#define _CUTEOS_KERNEL_USER_RETURN_H

/**
 * @file user_return.h
 * @brief Generic work run before returning from kernel to userspace.
 */

#include <kernel/compiler.h>
#include <kernel/types.h>

struct trap_frame;

/**
 * @brief Run pending work before resuming a user-mode trap frame.
 * @param tf User trap frame that will be restored by the arch trap return.
 */
void __nonnull(1) user_return_work(struct trap_frame *tf);

#ifdef CONFIG_KERNEL_TEST
typedef void (*user_return_test_hook_t)(struct trap_frame *tf);

void user_return_set_test_hook(user_return_test_hook_t hook);
#endif

#endif

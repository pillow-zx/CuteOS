/*
 * kernel/user_return.c - generic user-return work boundary
 */

#include <kernel/user_return.h>
#include <kernel/exit.h>
#include <kernel/rseq.h>
#include <kernel/signal.h>
#include <uapi/signal.h>

#ifdef CONFIG_KERNEL_TEST
static user_return_test_hook_t user_return_test_hook;

void user_return_set_test_hook(user_return_test_hook_t hook)
{
	user_return_test_hook = hook;
}
#endif

void user_return_work(struct trap_frame *tf)
{
	if (rseq_resume_user(tf) < 0)
		do_exit(SIGNAL_EXIT_CODE(SIGSEGV));

#ifdef CONFIG_KERNEL_TEST
	if (user_return_test_hook)
		user_return_test_hook(tf);
#endif

	do_signal(tf);
	/* Future syscall restart handling belongs here. */
	/* Future generic pending user-return work belongs here. */
}

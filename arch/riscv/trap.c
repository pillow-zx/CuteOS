/*
 * arch/riscv/trap.c - Trap 分发（C 层）
 */

#include <asm/csr.h>
#include <asm/trap.h>
#include <kernel/trap.h>
#include <kernel/printk.h>
#include <kernel/types.h>
#include <kernel/sched.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <kernel/user_return.h>

#ifdef KERNEL_SELFTEST
static trap_test_hook_t trap_test_hook;
#endif

static const char *trap_origin(const struct trap_frame *tf)
{
	return trap_frame_from_user(tf) ? "user" : "kernel";
}

#ifdef KERNEL_SELFTEST
void trap_set_hook(trap_test_hook_t hook)
{
	trap_test_hook = hook;
}
#endif

static void handle_timer_irq(void)
{
	uint64_t now = arch_timer_now();

	jiffies++;
	arch_timer_set(now + CLOCKS_PER_TICK);
	timer_run_expired(now);

	sched_tick();
}

void trap_handler(struct trap_frame *tf)
{
	uint64_t scause = trap_frame_cause(tf);
	bool is_interrupt = (scause & SCAUSE_IRQ_FLAG) != 0;
	uint64_t code = scause & ~SCAUSE_IRQ_FLAG;
	bool user = trap_frame_from_user(tf);

	if (current_task())
		task_set_trap_frame(current_task(), tf);

#ifdef KERNEL_SELFTEST
	if (trap_test_hook && trap_test_hook(tf))
		return;
#endif

	if (is_interrupt) {
		switch (code) {
		case IRQ_S_TIMER:
			handle_timer_irq();
			if (user && current_task() &&
			    task_need_resched(current_task())) {
				task_set_need_resched(current_task(), 0);
				schedule();
			}
			if (user)
				user_return_work(tf);
			return;
		default:
			panic("unhandled interrupt: origin=%s scause=0x%lx "
			      "code=%lu "
			      "sepc=%p stval=%p",
			      trap_origin(tf), (size_t)scause, (size_t)code,
			      (void *)trap_user_pc(tf),
			      (void *)trap_fault_addr(tf));
		}
	} else {
		switch (code) {
		case EXC_ECALL_U:

			trap_advance_pc(tf, 4);
			do_syscall(tf);
			if (user)
				user_return_work(tf);
			return;
		case EXC_INST_PAGE_FAULT:
		case EXC_LOAD_PAGE_FAULT:
		case EXC_STORE_PAGE_FAULT:

			do_page_fault(tf);
			if (user)
				user_return_work(tf);
			return;
		default:
			panic("unhandled exception: origin=%s scause=0x%lx "
			      "code=%lu "
			      "sepc=%p stval=%p",
			      trap_origin(tf), (size_t)scause, (size_t)code,
			      (void *)trap_user_pc(tf),
			      (void *)trap_fault_addr(tf));
		}
	}
}

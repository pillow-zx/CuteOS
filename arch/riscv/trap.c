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
#include <kernel/exit.h>
#include <kernel/signal.h>
#include <kernel/user_return.h>

#ifdef KERNEL_SELFTEST
static trap_test_hook_t trap_test_hook;
#endif

static const char *trap_origin(const struct trap_frame *tf)
{
	return trap_frame_from_user(tf) ? "user" : "kernel";
}

static siginfo_t trap_fault_info(int sig, int code, uintptr_t addr)
{
	siginfo_t info = {0};

	info.si_signo = sig;
	info.si_code = code;
	info.si_addr = (void *)addr;
	return info;
}

struct trap_exception trap_classify_exception(const struct trap_frame *tf)
{
	struct trap_exception exception = {0};
	uint64_t cause = trap_frame_cause(tf) & ~SCAUSE_IRQ_FLAG;

	if (cause == EXC_INST_PAGE_FAULT || cause == EXC_LOAD_PAGE_FAULT ||
	    cause == EXC_STORE_PAGE_FAULT) {
		exception.disposition = TRAP_EXCEPTION_PAGE_FAULT;
		return exception;
	}

	if (!trap_frame_from_user(tf)) {
		exception.disposition = TRAP_EXCEPTION_KERNEL_FATAL;
		return exception;
	}

	switch (cause) {
	case EXC_ECALL_U:
		exception.disposition = TRAP_EXCEPTION_SYSCALL;
		break;
	case EXC_INST_MISALIGNED:
	case EXC_LOAD_MISALIGNED:
	case EXC_STORE_MISALIGNED:
		exception.disposition = TRAP_EXCEPTION_USER_SIGNAL;
		exception.info = trap_fault_info(SIGBUS, BUS_ADRALN,
						 trap_fault_addr(tf));
		break;
	case EXC_INST_ACCESS:
	case EXC_LOAD_ACCESS:
	case EXC_STORE_ACCESS:
		exception.disposition = TRAP_EXCEPTION_USER_SIGNAL;
		exception.info = trap_fault_info(SIGSEGV, SEGV_ACCERR,
						 trap_fault_addr(tf));
		break;
	case EXC_INST_ILLEGAL:
		exception.disposition = TRAP_EXCEPTION_USER_SIGNAL;
		exception.info =
			trap_fault_info(SIGILL, ILL_ILLOPC, trap_user_pc(tf));
		break;
	case EXC_BREAKPOINT:
		exception.disposition = TRAP_EXCEPTION_USER_SIGNAL;
		exception.info =
			trap_fault_info(SIGTRAP, TRAP_BRKPT, trap_user_pc(tf));
		break;
	case EXC_ECALL_S:
		exception.disposition = TRAP_EXCEPTION_USER_SIGNAL;
		exception.info =
			trap_fault_info(SIGILL, ILL_ILLTRP, trap_user_pc(tf));
		break;
	default:
		exception.disposition = TRAP_EXCEPTION_USER_SIGNAL;
		exception.info =
			trap_fault_info(SIGILL, SI_KERNEL, trap_user_pc(tf));
		break;
	}

	return exception;
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
		struct trap_exception exception = trap_classify_exception(tf);

		switch (exception.disposition) {
		case TRAP_EXCEPTION_SYSCALL:
			trap_advance_pc(tf, 4);
			do_syscall(tf);
			user_return_work(tf);
			return;
		case TRAP_EXCEPTION_PAGE_FAULT:
			do_page_fault(tf);
			if (user)
				user_return_work(tf);
			return;
		case TRAP_EXCEPTION_USER_SIGNAL:
			if (exception.info.si_code == SI_KERNEL)
				pr_warn("unknown user exception: scause=0x%lx "
					"sepc=%p stval=%p pid=%d\n",
					(size_t)scause,
					(void *)trap_user_pc(tf),
					(void *)trap_fault_addr(tf),
					task_pid(current_task()));
			if (force_signal_info(exception.info.si_signo,
					      &exception.info,
					      current_task()) < 0)
				do_exit(SIGNAL_EXIT_CODE(
					exception.info.si_signo));
			user_return_work(tf);
			return;
		case TRAP_EXCEPTION_KERNEL_FATAL:
			panic("unhandled exception: origin=%s scause=0x%lx "
			      "code=%lu "
			      "sepc=%p stval=%p",
			      trap_origin(tf), (size_t)scause, (size_t)code,
			      (void *)trap_user_pc(tf),
			      (void *)trap_fault_addr(tf));
		}
	}
}

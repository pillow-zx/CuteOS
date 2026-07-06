/*
 * arch/riscv/trap.c - Trap 分发（C 层）
 *
 * 在汇编层保存完 trap_frame 后，由 trap_handler() 接住异常和中断。
 * 根据中断/异常类型分发到对应的处理函数。
 *
 * 当前分发：
 *   - Supervisor Timer Interrupt (scause = 0x8000000000000005)
 *     → handle_timer_irq(): 更新 jiffies, 设置下一次时钟中断
 *   - ecall from U-mode (scause = 8)
 *     → do_syscall(): 系统调用分发
 *   - 其他中断/异常 → panic（开发期）
 */

#include <asm/csr.h>
#include <asm/trap.h>
#include <kernel/trap.h>
#include <kernel/exit.h>
#include <kernel/printk.h>
#include <kernel/types.h>
#include <kernel/sched.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <kernel/rseq.h>
#include <kernel/signal.h>

static trap_test_hook_t trap_test_hook;

static const char *trap_origin(const struct trap_frame *tf)
{
	return trap_frame_from_user(tf) ? "user" : "kernel";
}

void trap_set_hook(trap_test_hook_t hook)
{
	trap_test_hook = hook;
}

/*
 * handle_timer_irq() - 时钟中断处理
 *
 * 每次时钟中断时调用：
 *   1. 递增全局 jiffies 计数器
 *   2. 通过 arch_timer_set 设置下一次时钟中断
 */
static void handle_timer_irq(void)
{
	uint64_t now = arch_timer_now();

	jiffies++;
	arch_timer_set(now + CLOCKS_PER_TICK);
	timer_run_expired(now);

	sched_tick();
}

static void trap_return_user_work(struct trap_frame *tf)
{
	if (rseq_resume_user(tf) < 0)
		do_exit(SIGNAL_EXIT_CODE(SIGSEGV));
	do_signal(tf);
}

/*
 * trap_handler() - 统一 trap 处理入口
 *
 * 由 entry.S 中的 __alltraps 调用，传入 trap_frame 指针。
 * 根据 scause 判断 trap 类型并分发。
 */
void trap_handler(struct trap_frame *tf)
{
	uint64_t scause = trap_frame_cause(tf);
	bool is_interrupt = (scause & SCAUSE_IRQ_FLAG) != 0;
	uint64_t code = scause & ~SCAUSE_IRQ_FLAG;
	bool user = trap_frame_from_user(tf);

	if (current_task())
		task_set_trap_frame(current_task(), tf);

	/* TODO: 待引入 Kconfig 后，将此测试 hook 置于
	 * CONFIG_TRAP_TEST_HOOK 编译期守卫之下，避免生产内核
	 * 在每次 trap 时承受间接调用的开销。
	 */
	if (trap_test_hook && trap_test_hook(tf))
		return;

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
				trap_return_user_work(tf);
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
			/* sepc +4 跳过 ecall 指令 */
			trap_advance_pc(tf, 4);
			do_syscall(tf);
			if (user)
				trap_return_user_work(tf);
			return;
		case EXC_INST_PAGE_FAULT:
		case EXC_LOAD_PAGE_FAULT:
		case EXC_STORE_PAGE_FAULT:
			/* 缺页异常：不修改 sepc，sret 后重新执行 */
			do_page_fault(tf);
			if (user)
				trap_return_user_work(tf);
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

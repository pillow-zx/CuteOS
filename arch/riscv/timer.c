/*
 * arch/riscv/timer.c - Sstc 时钟 (100Hz)
 *
 * 基于 RISC-V Sstc 扩展的定时器管理。
 * 使用 100Hz 固定频率（HZ=100，即每 10ms 一次）驱动时钟中断，
 * 为内核调度器和时间管理提供心跳。
 *
 * Sstc 扩展允许 S 模式直接通过 CSR 操作定时器，无需 ecall 陷入 M 模式：
 *   - time CSR:     读取当前 mtime 计数值
 *   - stimecmp CSR: 设置下一次时钟中断的比较值
 *
 * arch_timer_init() 仅负责设置首次 stimecmp 比较值。
 * 中断使能（SIE.STIE、sstatus.SIE）由 arch_trap_init() 统一管理。
 *
 * 常量和函数声明见 include/kernel/timer.h。
 */

#include <kernel/timer.h>
#include <kernel/errno.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/wait.h>

volatile uint64_t jiffies = 0;

/*
 * arch_timer_now - 通过 time CSR 读取当前时间计数器
 */
uint64_t arch_timer_now(void)
{
	return csr_read(time);
}

/*
 * arch_timer_set - 通过 stimecmp CSR 设置下一次时钟中断
 */
void arch_timer_set(uint64_t value)
{
	csr_write(stimecmp, value);
}

void timer_run_expired(uint64_t now)
{
	wait_timer_run_expired(now);
	ktimer_run_expired(now);
}

int timer_sleep_until(uint64_t expires, bool interruptible)
{
	if (!current)
		return -EINVAL;
	if (interruptible && signal_pending(current))
		return -EINTR;
	if (expires <= arch_timer_now())
		return 0;

	if (interruptible) {
		task_mark_interruptible_sleep(current);
		return wait_schedule_until(TASK_INTERRUPTIBLE, expires);
	}
	task_mark_uninterruptible_sleep(current);
	return wait_schedule_until(TASK_UNINTERRUPTIBLE, expires);
}

/*
 * arch_timer_init - 设置首次时钟中断超时值
 *
 * 仅配置 stimecmp，中断使能由 arch_trap_init() 负责。
 */
void arch_timer_init(void)
{
	arch_timer_set(arch_timer_now() + CLOCKS_PER_TICK);
}

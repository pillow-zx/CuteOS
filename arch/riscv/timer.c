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
 * timer_init() 仅负责设置首次 stimecmp 比较值。
 * 中断使能（SIE.STIE、sstatus.SIE）由 trap_init() 统一管理。
 *
 * 常量和函数声明见 include/kernel/timer.h。
 */

#include <kernel/timer.h>
#include <kernel/errno.h>
#include <kernel/signal.h>
#include <kernel/sched.h>
#include <kernel/sync.h>
#include <kernel/task.h>
#include <asm/csr.h>

volatile uint64_t jiffies = 0;

static struct {
	spinlock_t lock;
	struct list_head entries;
} timer_wait_queue = {
	.lock = SPINLOCK_INIT,
	.entries = LIST_HEAD_INIT(timer_wait_queue.entries),
};

/*
 * get_mtime - 通过 time CSR 读取当前时间计数器
 */
uint64_t get_mtime(void)
{
	return csr_read(time);
}

/*
 * set_mtimecmp - 通过 stimecmp CSR 设置下一次时钟中断
 */
void set_mtimecmp(uint64_t value)
{
	csr_write(stimecmp, value);
}

void timer_wait_init(struct timer_wait *wait, struct task_struct *task,
		     uint64_t expires)
{
	BUG_ON(!wait);

	INIT_LIST_HEAD(&wait->node);
	wait->task = task;
	wait->expires = expires;
	wait->active = false;
	wait->fired = false;
}

void timer_wait_start(struct timer_wait *wait)
{
	irq_flags_t flags;

	BUG_ON(!wait);
	BUG_ON(!wait->task);

	spin_lock_irqsave(&timer_wait_queue.lock, &flags);
	BUG_ON(wait->active);
	wait->active = true;
	wait->fired = false;
	list_add_tail(&wait->node, &timer_wait_queue.entries);
	spin_unlock_irqrestore(&timer_wait_queue.lock, flags);
}

bool timer_wait_cancel(struct timer_wait *wait)
{
	irq_flags_t flags;
	bool fired;

	if (!wait)
		return false;

	spin_lock_irqsave(&timer_wait_queue.lock, &flags);
	fired = wait->fired;
	if (wait->active) {
		list_del_init(&wait->node);
		wait->active = false;
	}
	spin_unlock_irqrestore(&timer_wait_queue.lock, flags);

	return !fired;
}

bool timer_wait_fired(const struct timer_wait *wait)
{
	return wait && wait->fired;
}

void timer_run_expired(uint64_t now)
{
	struct list_head *pos;
	struct list_head *next;
	irq_flags_t flags;

	spin_lock_irqsave(&timer_wait_queue.lock, &flags);
	list_for_each_safe (pos, next, &timer_wait_queue.entries) {
		struct timer_wait *wait =
			list_entry(pos, struct timer_wait, node);

		if (wait->expires > now)
			continue;

		list_del_init(&wait->node);
		wait->active = false;
		wait->fired = true;

		if (wait->task &&
		    (wait->task->state == TASK_SLEEPING ||
		     wait->task->state == TASK_INTERRUPTIBLE)) {
			wait->task->state = TASK_RUNNING;
			if (wait->task != current)
				sched_wakeup(wait->task);
		}
	}
	spin_unlock_irqrestore(&timer_wait_queue.lock, flags);
}

int timer_sleep_until(uint64_t expires, bool interruptible)
{
	struct timer_wait wait;
	bool local_timer_wait;
	bool enabled_irq_for_sleep = false;

	if (!current)
		return -EINVAL;
	if (interruptible && signal_pending(current))
		return -EINTR;
	if (expires <= get_mtime())
		return 0;

	local_timer_wait = !sched_has_runnable();
	current->state = interruptible ? TASK_INTERRUPTIBLE : TASK_SLEEPING;
	timer_wait_init(&wait, current, expires);
	timer_wait_start(&wait);

	if (irqs_disabled()) {
		csr_set(sstatus, SSTATUS_SIE);
		enabled_irq_for_sleep = true;
	}
	if (local_timer_wait) {
		while (!timer_wait_fired(&wait) &&
		       !(interruptible && signal_pending(current)))
			wfi();
	} else {
		schedule();
	}
	if (enabled_irq_for_sleep)
		csr_clear(sstatus, SSTATUS_SIE);

	timer_wait_cancel(&wait);
	if (current->state == TASK_SLEEPING ||
	    current->state == TASK_INTERRUPTIBLE)
		current->state = TASK_RUNNING;

	if (interruptible && signal_pending(current))
		return -EINTR;
	return 0;
}

/*
 * timer_init - 设置首次时钟中断超时值
 *
 * 仅配置 stimecmp，中断使能由 trap_init() 负责。
 */
void timer_init(void)
{
	set_mtimecmp(get_mtime() + CLOCKS_PER_TICK);
}

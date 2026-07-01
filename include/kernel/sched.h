#ifndef _CUTEOS_KERNEL_SCHED_H
#define _CUTEOS_KERNEL_SCHED_H

/*
 * include/kernel/sched.h - 调度器与上下文切换
 *
 * 声明调度器核心数据结构与接口。struct context 保存被调用者保存的
 * 寄存器，用于内核线程与用户进程之间的协作式上下文切换。
 *
 * struct context 在 asm/trap.h 中定义：
 *   ra, sp, s0 through s11  (14 registers, 112 bytes)
 *
 * Functions:
 *   schedule()  - Pick the next runnable task and switch to it
 *   switch_to(prev, next) - Low-level context switch (asm)
 *
 * Policy:
 *   The scheduler uses a single-core 4-level MLFQ policy.
 *
 * Preemption stubs (currently no-op):
 *   preempt_disable()
 *   preempt_enable()
 */

#include <kernel/list.h>
#include <kernel/task.h>
#include <asm/trap.h>

/* ---- MLFQ 参数 ---- */

#define SCHED_MLFQ_LEVELS 4

/* ---- 调度器函数声明 ---- */

/**
 * sched_init - 初始化调度器
 *
 * 初始化全局就绪队列。
 */
void sched_init(void);

/**
 * schedule - 主调度函数
 *
 * 从就绪队列取队首进程，将当前进程放回队尾，
 * 校验 canary 完整性，调用 switch_to 进行上下文切换。
 */
void schedule(void);

/**
 * sched_tick - timer tick 调度计费
 *
 * 由时钟中断处理调用。当前只在 trap 返回用户态前根据 need_resched
 * 实际切换，sched_tick 只负责策略状态更新和置位。
 */
void sched_tick(void);

/**
 * sched_yield - 当前任务主动让出 CPU
 *
 * 当前任务按同级队尾重排，不主动降级，不刷新剩余时间片。
 */
void sched_yield(void);

/**
 * sched_task_init - 初始化 task_struct 内的调度字段
 * @task: 新分配或静态初始化的任务
 */
void sched_task_init(struct task_struct *task);

/**
 * sched_enqueue - 将进程加入就绪队列尾部
 * @task: 要入队的任务
 */
void sched_enqueue(struct task_struct *task);

/**
 * sched_wakeup - 唤醒任务并按策略入队
 * @task: 被唤醒任务
 *
 * 保持当前 MLFQ 等级，刷新该等级完整时间片；若已在队列中则不重复入队。
 */
void sched_wakeup(struct task_struct *task);
bool sched_has_runnable(void);

/**
 * sched_wake_task - 将睡眠任务转为可运行状态
 * @task: 被唤醒任务
 *
 * 统一封装 TASK_RUNNING 状态切换和非当前任务入队。sched_wakeup()
 * 仍保留为底层 MLFQ 入队接口。
 */
void sched_wake_task(struct task_struct *task);

/**
 * sched_dequeue - 将进程从就绪队列中移除
 * @task: 要出队的任务
 */
void sched_dequeue(struct task_struct *task);

/* ---- 抢占控制 ---- */

/**
 * preempt_count - 抢占计数器
 *
 * 非 0 时禁止调度。中断处理中调用 schedule() 前应检查 preemptible()。
 * 每个 preempt_disable() 递增，preempt_enable() 递减。
 * 嵌套安全：可以多次 disable/enable 配对使用。
 */
extern volatile int preempt_count;

#define preempt_disable()                                                      \
	do {                                                                   \
		preempt_count++;                                               \
	} while (0)
#define preempt_enable()                                                       \
	do {                                                                   \
		preempt_count--;                                               \
	} while (0)

/**
 * preemptible - 检查当前是否允许调度
 *
 * 当 preempt_count == 0 时返回 true。
 */
static __always_inline bool preemptible(void)
{
	return preempt_count == 0;
}

#ifdef CONFIG_KERNEL_TEST
bool sched_test_runqueue_empty(void);
uint32_t sched_test_runnable_count(void);
struct task_struct *sched_test_peek_next(void);
void sched_test_force_boost(void);
uint8_t sched_test_level_slice(uint8_t level);
uint8_t sched_test_level(const struct task_struct *task);
uint8_t sched_test_time_slice(const struct task_struct *task);
uint8_t sched_test_ticks(const struct task_struct *task);
uint8_t sched_test_need_resched(const struct task_struct *task);
void sched_test_set_level(struct task_struct *task, uint8_t level);
void sched_test_set_budget(struct task_struct *task, uint8_t slice,
			   uint8_t ticks);
#endif

#endif

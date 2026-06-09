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
 * Runqueue:
 *   The global runqueue is a simple list of runnable tasks.
 *
 * Preemption stubs (currently no-op):
 *   preempt_disable()
 *   preempt_enable()
 */

#include <kernel/list.h>
#include <kernel/task.h>
#include <asm/trap.h>

/* ---- 全局就绪队列 ---- */

extern struct list_head runqueue;

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
 * sched_enqueue - 将进程加入就绪队列尾部
 * @task: 要入队的任务
 */
void sched_enqueue(struct task_struct *task);

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

#endif

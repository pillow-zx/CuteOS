/*
 * kernel/sched.c - FIFO 调度器
 *
 * 实现简单的全局 FIFO（先来先服务）调度器。维护一条全局就绪队列
 * （链表实现），schedule() 从队首取第一个可运行进程投入运行，
 * 将当前进程放回队尾，然后调用 switch_to 完成上下文切换。
 *
 * 时钟中断处理（handle_timer_irq，在 trap.c 中）：
 *   - 递增全局 jiffies 计数器。
 *   - 设置下一次时钟中断。
 *   - 若当前进程非 idle 且状态为 RUNNING，设置 need_resched 标志，
 *     在 trap 返回前触发 schedule() 实现时间片轮转。
 *
 * 栈溢出检测：每次 schedule() 切换前检查当前进程的内核栈 canary
 * （check_canary），若 canary 被破坏则触发 panic。
 *
 * 抢占控制：preempt_disable / preempt_enable 当前为空宏，
 * 暂不实现抢占计数，后续可扩展。
 *
 * 主要函数：
 *   sched_init()        - 初始化调度器（就绪队列）
 *   sched_enqueue(task) - 将进程加入就绪队列尾部
 *   sched_dequeue(task) - 将进程从就绪队列中移除
 *   schedule()          - 主调度函数。取队首进程，当前进程回队尾，
 *                         check_canary 校验栈完整性，
 *                         调用 switch_to 进行上下文切换。
 */

#include <kernel/sched.h>
#include <kernel/printk.h>

/* ---- 全局就绪队列 ---- */

struct list_head runqueue;

/* ---- 抢占计数器 ---- */

volatile int preempt_count;

/**
 * sched_init - 初始化调度器
 *
 * 初始化全局就绪队列为空。
 * 在 kernel_main() 中、task_init() 和 timer_init() 之后调用。
 */
void sched_init(void)
{
        INIT_LIST_HEAD(&runqueue);
        printk("sched: runqueue initialized\n");
}

/**
 * sched_enqueue - 将进程加入就绪队列尾部
 * @task: 要入队的任务
 */
void sched_enqueue(struct task_struct *task)
{
        BUG_ON(!list_empty(&task->run_list));
        list_add_tail(&task->run_list, &runqueue);
}

/**
 * sched_dequeue - 将进程从就绪队列中移除
 * @task: 要出队的任务
 */
void sched_dequeue(struct task_struct *task)
{
        list_del(&task->run_list);
}

/**
 * schedule - 主调度函数
 *
 * FIFO 策略：
 *   1. 就绪队列为空 → 直接返回（idle 继续运行）
 *   2. 取队首进程作为 next，dequeue
 *   3. 若当前进程非 idle 且状态为 RUNNING → 重新入队到尾部
 *   4. check_canary 校验当前进程栈完整性
 *   5. 更新 current 指针
 *   6. switch_to 完成上下文切换
 *
 * WARN:
 *   非 RUNNING 的进程不入队 — 调用者需通过 wakeup/sched_enqueue 重新唤醒
 */
void schedule(void)
{
        if (!preemptible())
                return;

        if (list_empty(&runqueue))
                return;

        struct task_struct *next =
                list_first_entry(&runqueue, struct task_struct, run_list);
        list_del(&next->run_list);

        struct task_struct *prev = current;

        /* 将当前进程重新入队，除非它是 idle 或即将睡眠 */
        if (prev != &idle_task && prev->state == TASK_RUNNING)
                list_add_tail(&prev->run_list, &runqueue);

        /* 切换前检查栈 canary */
        check_canary(prev);

        /* 更新当前进程指针 */
        current = next;

        /* 上下文切换 */
        switch_to(&prev->ctx, &next->ctx);
}

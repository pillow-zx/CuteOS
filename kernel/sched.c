/*
 * kernel/sched.c - FIFO 调度器
 *
 * 功能：
 *   实现简单的全局 FIFO（先来先服务）调度器。维护一条全局就绪队列
 *   （链表实现），schedule() 从队首取第一个可运行进程投入运行，
 *   将当前进程放回队尾，然后调用 switch_to 完成上下文切换。
 *
 *   时钟中断处理（handle_timer_irq）：
 *     - 递增全局 jiffies 计数器。
 *     - 调用 sbi_set_timer 设置下一次时钟中断。
 *     - 若当前进程非 idle 且状态为 RUNNING，设置 need_resched 标志，
 *       在 trap 返回前触发 schedule() 实现时间片轮转。
 *
 *   栈溢出检测：每次 schedule() 切换前检查当前进程的内核栈 canary
 *   （check_canary），若 canary 被破坏则触发 panic。
 *
 *   抢占控制：preempt_disable / preempt_enable 当前为空宏，
 *   暂不实现抢占计数，后续可扩展。
 *
 * 主要函数：
 *   sched_init()        - 初始化调度器（就绪队列、当前运行的 idle 进程等）
 *   sched_enqueue(task) - 将进程加入就绪队列尾部
 *   sched_dequeue(task) - 将进程从就绪队列中移除
 *   schedule()          - 主调度函数。取队首进程，当前进程回队尾，
 *                         check_canary 校验栈完整性，
 *                         调用 switch_to 进行上下文切换。
 *   handle_timer_irq()  - 时钟中断调度处理：
 *                         jiffies++，sbi_set_timer 设下一次中断，
 *                         非 idle RUNNING 进程设 need_resched。
 *   preempt_disable()   - 禁用抢占（当前为空宏）
 *   preempt_enable()    - 启用抢占（当前为空宏）
 */

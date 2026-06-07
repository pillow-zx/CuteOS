#ifndef _CUTEOS_KERNEL_SCHED_H
#define _CUTEOS_KERNEL_SCHED_H

/*
 * include/kernel/sched.h - 调度器与上下文切换
 *
 * 声明调度器核心数据结构与接口。struct context 保存被调用者保存的
 * 寄存器，用于内核线程与用户进程之间的协作式上下文切换。
 *
 * struct context - Callee-saved register save area (120 bytes):
 *   ra, sp, s0 through s11  (15 registers total)
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

#endif

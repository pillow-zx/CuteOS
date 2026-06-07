#ifndef _CUTEOS_KERNEL_ATOMIC_H
#define _CUTEOS_KERNEL_ATOMIC_H

/*
 * include/kernel/atomic.h - 原子整数操作
 *
 * 提供 atomic_t 及基本的原子读/设/加一/减一/加操作。
 * cuteOS 面向单核系统，因此原子性通过关中断而非使用
 * RISC-V 原子扩展（A 扩展）指令实现。
 *
 * 中断控制：
 *   local_irq_disable()  - 关中断（清除 sstatus 中的 SIE）
 *   local_irq_enable()   - 开中断（设置 sstatus 中的 SIE）
 *
 * atomic_t 原子操作（volatile int 计数器）：
 *   atomic_read(v)   - 读取计数器值
 *   atomic_set(v, i) - 将计数器设为 i
 *   atomic_inc(v)    - 加 1
 *   atomic_dec(v)    - 减 1
 *   atomic_add(v, i) - 将计数器加 i
 */

#endif

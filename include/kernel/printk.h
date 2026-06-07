#ifndef _CUTEOS_KERNEL_PRINTK_H
#define _CUTEOS_KERNEL_PRINTK_H

/*
 * include/kernel/printk.h - 内核日志、panic 与断言
 *
 * 声明内核诊断输出函数。printk 提供 printf 风格的格式化控制台输出；
 * panic 在致命错误时停机；BUG_ON 和 ASSERT 为条件检查宏。
 *
 * Declarations:
 *   printk(fmt, ...)  - Formatted kernel log output
 *   panic(fmt, ...)   - Print message and halt the kernel
 *   BUG_ON(cond)      - Trigger panic if condition is true
 *   ASSERT(cond)      - Assert condition, panic if false
 */

#endif

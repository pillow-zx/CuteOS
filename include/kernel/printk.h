/*
 * include/kernel/printk.h - 内核日志、panic 与断言
 *
 * 声明内核诊断输出函数。printk 提供 printf 风格的格式化控制台输出；
 * panic 在致命错误时停机；BUG_ON 和 ASSERT 为条件检查宏。
 *
 * 同时提供 va_list 定义（基于 GCC __builtin_va_list），
 * 供 vsprintf/vsnprintf 使用，避免依赖 libc 的 <stdarg.h>。
 *
 * Declarations:
 *   console_init_sbi()    - 将 printk 底层绑定到 SBI ecall
 *   vsnprintf()           - 带缓冲区大小限制的格式化
 *   vsprintf()            - 无限制格式化
 *   printk(fmt, ...)      - Formatted kernel log output
 *   panic(fmt, ...)       - Print message and halt the kernel
 *   BUG_ON(cond)          - Trigger panic if condition is true
 *   ASSERT(cond)          - Assert condition, panic if false
 */

#ifndef _CUTEOS_KERNEL_PRINTK_H
#define _CUTEOS_KERNEL_PRINTK_H

#include <kernel/types.h>
#include <kernel/compiler.h>

/* va_list — GCC 内建可变参数支持，不依赖 libc */
typedef __builtin_va_list	va_list;
#define va_start(ap, last)	__builtin_va_start(ap, last)
#define va_end(ap)		__builtin_va_end(ap)
#define va_arg(ap, type)	__builtin_va_arg(ap, type)

/* Console initialization */
void console_init_sbi(void);
void console_init_mmio(void);

/* Formatted output */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int printk(const char *fmt, ...) __printf(1, 2);

/* Panic and assertions */
void __noreturn panic(const char *fmt, ...) __printf(1, 2) __cold;

#define BUG_ON(cond)                                                           \
	do {                                                                   \
		if (unlikely(cond))                                            \
			panic("BUG: %s:%d %s\n", __FILE__, __LINE__, #cond);   \
	} while (0)

#define ASSERT(cond)                                                           \
	do {                                                                   \
		if (unlikely(!(cond)))                                         \
			panic("ASSERT: %s:%d %s\n", __FILE__, __LINE__,        \
			      #cond);                                          \
	} while (0)

#endif

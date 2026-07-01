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
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)	   __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

#define ANSI_FG_BLACK	"\33[1;30m"
#define ANSI_FG_RED	"\33[1;31m"
#define ANSI_FG_GREEN	"\33[1;32m"
#define ANSI_FG_YELLOW	"\33[1;33m"
#define ANSI_FG_BLUE	"\33[1;34m"
#define ANSI_FG_MAGENTA "\33[1;35m"
#define ANSI_FG_CYAN	"\33[1;36m"
#define ANSI_FG_WHITE	"\33[1;37m"
#define ANSI_BG_BLACK	"\33[1;40m"
#define ANSI_BG_RED	"\33[1;41m"
#define ANSI_BG_GREEN	"\33[1;42m"
#define ANSI_BG_YELLOW	"\33[1;43m"
#define ANSI_BG_BLUE	"\33[1;44m"
#define ANSI_BG_MAGENTA "\33[1;45m"
#define ANSI_BG_CYAN	"\33[1;46m"
#define ANSI_BG_WHITE	"\33[1;47m"
#define ANSI_NONE	"\33[0m"

#define LOG_ALL	    0
#define LOG_DEBUG   1
#define LOG_INFO    2
#define LOG_NOTICE  3
#define LOG_WARNING 4
#define LOG_ERROR   5

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_INFO
#endif

/* Console initialization */
void console_init_sbi(void);
void console_init_mmio(void);
size_t log_buffer_size(void);

/* Formatted output */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int __printk(const char *fmt, ...) __printf(1, 2) __nonnull(1);
void __noreturn __panic(const char *fmt, ...) __printf(1, 2) __nonnull(1)
	__cold;

#define _Log(level, ...)                                                       \
	do {                                                                   \
		if ((level) >= LOG_LEVEL)                                      \
			__printk(__VA_ARGS__);                                 \
	} while (0)

#define ANSI_FMT(str) str ANSI_NONE
#define LOG_COLOR(level)                                                       \
	((level) == LOG_ERROR	  ? ANSI_FG_RED                                \
	 : (level) == LOG_WARNING ? ANSI_FG_YELLOW                             \
	 : (level) == LOG_NOTICE  ? ANSI_FG_CYAN                               \
	 : (level) == LOG_INFO	  ? ANSI_FG_GREEN                              \
	 : (level) == LOG_DEBUG	  ? ANSI_FG_BLUE                               \
				  : ANSI_NONE)

#define printk(level, fmt, ...)                                                \
	do {                                                                   \
		_Log(level, ANSI_FMT("%s" fmt), LOG_COLOR((level)),            \
		     ##__VA_ARGS__);                                           \
	} while (0)

#define pr_err(fmt, ...)    printk(LOG_ERROR, fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)   printk(LOG_WARNING, fmt, ##__VA_ARGS__)
#define pr_notice(fmt, ...) printk(LOG_NOTICE, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)   printk(LOG_INFO, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)  printk(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define panic(fmt, ...)	    __panic(fmt, ##__VA_ARGS__)

/* Hex dump: 按 offset + 十六进制 + ASCII 形式输出 len 字节（每行 16 字节） */
void print_hexdump(const void *buf, size_t len);
void dump_stack(void);

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

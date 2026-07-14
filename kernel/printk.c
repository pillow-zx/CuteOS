/*
 * kernel/printk.c - 内核日志与格式化输出
 */

#include <kernel/printk.h>
#include <kernel/stacktrace.h>
#include <kernel/processor.h>
#include <kernel/compiler.h>
#include <kernel/sbi.h>
#include <drivers/uart.h>

static void (*console_putc)(int ch);

constexpr size_t PRINTK_BUF_SIZE = 1024;
static char printk_buf[PRINTK_BUF_SIZE];

constexpr size_t PRINTK_LOG_BUF_SIZE = 4096;

static void console_write(const char *s)
{
	while (*s) {
		if (*s == '\n')
			console_putc('\r');
		console_putc(*s++);
	}
}

void console_init_sbi(void)
{
	console_putc = sbi_console_putchar;
}

void console_init_mmio(void)
{
	uart_init();
	console_putc = uart_putc;
}

size_t log_buffer_size(void)
{
	return PRINTK_LOG_BUF_SIZE;
}

int __printk(const char *fmt, ...)
{
	if (!console_putc)
		return 0;

	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(printk_buf, PRINTK_BUF_SIZE, fmt, ap);
	va_end(ap);

	console_write(printk_buf);
	return n;
}

void __noreturn __panic(const char *fmt, ...)
{
	pr_err("\nKERNEL PANIC: ");

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(printk_buf, PRINTK_BUF_SIZE, fmt, ap);
	va_end(ap);
	console_write(printk_buf);
	pr_err("\n");


	pr_err("  sepc   = %p\n", (void *)(uintptr_t)trap_pc());
	pr_err("  scause = %p\n", (void *)(uintptr_t)trap_cause());
	pr_err("  stval  = %p\n", (void *)(uintptr_t)trap_value());
	pr_err("  ra     = %p\n", (void *)(uintptr_t)__return_address());
	pr_err("  sp     = %p\n", (void *)(uintptr_t)__frame_address());
	dump_stack();


	while (1)
		wait_for_interrupt();

	unreachable();
}

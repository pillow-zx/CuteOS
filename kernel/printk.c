/*
 * kernel/printk.c - 内核日志与格式化输出
 *
 * 功能：
 *   提供内核日志输出基础设施。printk 不区分日志级别，所有输出均直接打印。
 *   vsprintf 作为格式化核心，支持以下转换说明符：
 *     %d  - 有符号十进制整数
 *     %x  - 无符号十六进制整数
 *     %s  - 字符串
 *     %c  - 单个字符
 *     %p  - 指针地址（十六进制）
 *     %%  - 百分号字面量
 *     %ld - long 有符号十进制
 *     %lu - long 无符号十进制
 *     %llx - long long 无符号十六进制
 *     %#x - 带前缀的十六进制（0x）
 *     %-Ns - 左对齐字符串，宽度 N
 *     %0Nd - 零填充十进制，宽度 N
 *
 * 主要函数：
 *   console_init_sbi()        - 将 printk 底层绑定到 SBI ecall 输出。
 *                               调用后 printk 即可使用。在正式页表建立前
 *                               即可调用，因为 SBI ecall 不依赖 MMIO 映射。
 *   printk(fmt, ...)          - 内核格式化打印，无日志级别过滤，
 *                               所有输出通过底层驱动送达控制台。
 *   panic(fmt, ...)           - 内核致命错误处理：打印 panic 信息，
 *                               输出关键 CSR
 * 寄存器值（sepc/scause/stval/ra/sp） 用于事后诊断，随后执行 wfi 指令永久挂起
 * CPU。
 *
 * 调试宏：
 *   ASSERT(cond)   - 检查条件，失败则调用 panic
 *   BUG_ON(cond)   - 若条件为真则触发 panic
 */

#include <kernel/printk.h>
#include <kernel/stacktrace.h>
#include <asm/csr.h>
#include <asm/sbi.h>
#include <drivers/uart.h>

/* 控制台字符输出后端，由 console_init_sbi() 设置 */
static void (*console_putc)(int ch);

/* printk 内部格式化缓冲区 */
#define PRINTK_BUF_SIZE 1024
static char printk_buf[PRINTK_BUF_SIZE];

/*
 * Planned kernel log ring size exposed to syslog(2) probes. printk currently
 * writes directly to the console; real ring-buffer reads are a later slice.
 */
#define PRINTK_LOG_BUF_SIZE 4096

/*
 * console_write - 将字符串输出到控制台
 * @s: 以 '\0' 结尾的字符串
 *
 * 逐字符输出，遇到 '\n' 自动补 '\r' 以兼容串口终端。
 */
static void console_write(const char *s)
{
	while (*s) {
		if (*s == '\n')
			console_putc('\r');
		console_putc(*s++);
	}
}

/*
 * console_init_sbi - 将 printk 底层绑定到 SBI ecall
 *
 * 调用 sbi_console_putchar 作为字符输出后端。
 * 必须在使用 printk() 之前调用，否则 printk 为空操作。
 * SBI ecall 不依赖 MMIO 映射，在临时页表阶段即可使用。
 */
void console_init_sbi(void)
{
	console_putc = sbi_console_putchar;
}

/*
 * console_init_mmio - 将 printk 底层切换到 UART MMIO
 *
 * 初始化 NS16550A UART 硬件，然后将 console_putc 指向
 * uart_putc。此后 printk 输出不再经过 SBI ecall，
 * 而是直接读写 UART 寄存器。
 * 必须在正式页表建立后调用（MMIO 映射可用）。
 */
void console_init_mmio(void)
{
	uart_init();
	console_putc = uart_putc;
}

size_t log_buffer_size(void)
{
	return PRINTK_LOG_BUF_SIZE;
}

/*
 * printk - 内核格式化打印
 * @fmt: 格式字符串（见 vsprintf 支持的格式符）
 * @...: 可变参数
 *
 * 返回写入的字符数（不含 '\0'）。
 * 如果 console_putc 未初始化，则为空操作。
 */
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

/*
 * print_hexdump - 十六进制转储
 * @buf: 数据缓冲
 * @len: 字节数
 *
 * 每行 16 字节：偏移 + 空格分隔的十六进制 + ASCII（不可打印字符显示 '.'）。
 */
void print_hexdump(const void *buf, size_t len)
{
	const uint8_t *p = buf;

	for (size_t off = 0; off < len; off += 16) {
		pr_info("%04x: ", (unsigned int)off);
		for (size_t i = 0; i < 16; i++) {
			if (off + i < len)
				pr_info("%02x ", p[off + i]);
			else
				pr_info("   ");
		}
		pr_info(" ");
		for (size_t i = 0; i < 16; i++) {
			if (off + i < len) {
				uint8_t c = p[off + i];
				pr_info("%c",
					(c >= 0x20 && c < 0x7f) ? c : '.');
			}
		}
		pr_info("\n");
	}
}

/*
 * panic - 内核致命错误处理
 * @fmt: 格式字符串
 * @...: 可变参数
 *
 * 打印 panic 信息及关键 CSR 寄存器值，然后永久挂起 CPU。
 * 此函数不会返回。
 */
void __noreturn __panic(const char *fmt, ...)
{
	pr_err("\nKERNEL PANIC: ");

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(printk_buf, PRINTK_BUF_SIZE, fmt, ap);
	va_end(ap);
	console_write(printk_buf);
	pr_err("\n");

	/* 输出关键 CSR 寄存器用于事后诊断 */
	pr_err("  sepc   = %p\n", (void *)(uintptr_t)csr_read(sepc));
	pr_err("  scause = %p\n", (void *)(uintptr_t)csr_read(scause));
	pr_err("  stval  = %p\n", (void *)(uintptr_t)csr_read(stval));
	pr_err("  ra     = %p\n", (void *)(uintptr_t)__return_address());
	pr_err("  sp     = %p\n", (void *)(uintptr_t)__frame_address());
	dump_stack();

	/* 永久挂起 */
	while (1)
		wfi();

	unreachable();
}

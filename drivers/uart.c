/*
 * drivers/uart.c - NS16550A UART 驱动
 *
 * 功能：
 *   实现 NS16550A UART（串口）驱动，基地址 0x10000000。作为内核
 *   早期调试和最终控制台的字符设备。支持两个初始化阶段以适配内核
 *   启动过程。
 *
 * 初始化阶段：
 *   console_init_sbi()  - 早期阶段，在页表建立前使用
 *             SBI ecall 进行串口访问。
 *   console_init_mmio() - 页表建立后切换到 MMIO 直接访问方式。
 *
 * 主要函数：
 *   uart_init()   - 初始化 UART：禁中断，8N1，使能并清空 FIFO。
 *   uart_putc(ch) - 输出单个字符。轮询 LSR 寄存器等待 THR 就绪后写入。
 *   uart_getc()   - 读入单个字符。轮询 LSR 寄存器等待 RBR 有数据后读取
 *            （忙等待方式）。早期 Stage 1-4 的读取均使用忙轮询。
 */

#include <drivers/uart.h>
#include <kernel/tools.h>

/* ---- MMIO 寄存器读写 ---- */

static __always_inline void uart_write_reg(int reg, uint8_t val)
{
	MMIO_WRITE(uint8_t, UART_BASE + reg, val);
}

static __always_inline uint8_t uart_read_reg(int reg)
{
	return MMIO_READ(uint8_t, UART_BASE + reg);
}

/* ---- 公共接口 ---- */

/*
 * uart_init - 初始化 NS16550A UART
 *
 * 配置为 8N1 模式（8 数据位、无校验、1 停止位），
 * 使能并清空 FIFO，禁用所有中断。
 * QEMU virt 平台不依赖波特率分频器设置（固定 115200），
 * 但仍按标准流程设置 DLAB 分频器。
 */
void uart_init(void)
{
	/* 禁用所有中断 */
	uart_write_reg(UART_IER, 0x00);

	/* 设置 DLAB=1，访问分频器 */
	uart_write_reg(UART_LCR, UART_LCR_DLAB);

	/* 分频器 = 1 (115200 baud @ QEMU) */
	uart_write_reg(0, 0x01); /* DLL */
	uart_write_reg(1, 0x00); /* DLM */

	/* 8N1，清除 DLAB */
	uart_write_reg(UART_LCR, UART_LCR_8N1);

	/* 使能 FIFO，清空收发 FIFO */
	uart_write_reg(UART_FCR, UART_FCR_EN | UART_FCR_CLR);

	/* Modem 控制寄存器：无需额外信号 */
	uart_write_reg(UART_MCR, 0x00);
}

/*
 * uart_putc - 轮询发送单个字符
 * @ch: 待发送字符
 *
 * 忙等 LSR.THR Empty 位为 1，然后写入 THR。
 * 使用 volatile 指针确保编译器不会优化掉轮询循环。
 */
void uart_putc(int ch)
{
	while (!(uart_read_reg(UART_LSR) & UART_LSR_THRE))
		;
	uart_write_reg(UART_THR, (uint8_t)ch);
}

/*
 * uart_getc - 轮询接收单个字符（忙等待）
 *
 * 返回值：读取到的字符（0~255）。
 * 忙等 LSR.Data Ready 位为 1，然后从 RBR 读取。
 */
int uart_getc(void)
{
	while (!(uart_read_reg(UART_LSR) & UART_LSR_DR))
		;
	return uart_read_reg(UART_RBR);
}

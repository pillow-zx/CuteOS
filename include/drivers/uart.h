/*
 * include/drivers/uart.h - NS16550A UART 驱动接口
 *
 * 功能：
 *   定义 NS16550A UART（串口）寄存器偏移与状态位，
 *   声明驱动初始化、字符输入输出函数。
 *
 *   QEMU virt 平台的 UART 基地址为 0x10000000，
 *   由内核页表 PGD[0] 的 1GB mega page 恒等映射覆盖。
 *
 * 寄存器偏移（字节，DLAB=0）：
 *   UART_THR  0  发送保持寄存器（写）
 *   UART_RBR  0  接收缓冲寄存器（读）
 *   UART_IER  1  中断使能寄存器
 *   UART_FCR  2  FIFO 控制寄存器（写）
 *   UART_LCR  3  线控制寄存器
 *   UART_MCR  4  modem 控制寄存器
 *   UART_LSR  5  线状态寄存器
 *
 * 主要函数：
 *   uart_init()   - 初始化 UART：8N1，使能 FIFO
 *   uart_putc(ch) - 轮询发送单个字符（等 THR 就绪）
 *   uart_getc()   - 轮询接收单个字符（忙等待）
 */

#ifndef _CUTEOS_DRIVERS_UART_H
#define _CUTEOS_DRIVERS_UART_H

#include <kernel/types.h>

/* UART 基地址 — MMIO 恒等映射区域 (PGD[0]) */
#define UART_BASE	0x10000000UL

/* NS16550A 寄存器偏移 */
#define UART_THR	0	/* Transmit Holding Register (write) */
#define UART_RBR	0	/* Receive Buffer Register (read) */
#define UART_IER	1	/* Interrupt Enable Register */
#define UART_FCR	2	/* FIFO Control Register (write) */
#define UART_LCR	3	/* Line Control Register */
#define UART_MCR	4	/* Modem Control Register */
#define UART_LSR	5	/* Line Status Register */

/* LSR 状态位 */
#define UART_LSR_DR	0x01	/* Data Ready */
#define UART_LSR_THRE	0x20	/* THR Empty */

/* LCR 位 */
#define UART_LCR_DLAB	0x80	/* Divisor Latch Access Bit */
#define UART_LCR_8N1	0x03	/* 8 data bits, no parity, 1 stop bit */

/* FCR 位 */
#define UART_FCR_EN	0x01	/* Enable FIFOs */
#define UART_FCR_CLR	0x06	/* Clear both FIFOs */

void uart_init(void);
void uart_putc(int ch);
int uart_getc(void);

#endif

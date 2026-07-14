/*
 * include/drivers/uart.h - NS16550A UART 驱动接口
 */

#ifndef _CUTEOS_DRIVERS_UART_H
#define _CUTEOS_DRIVERS_UART_H

#include <kernel/types.h>

constexpr uintptr_t UART_BASE = 0x10000000UL;

constexpr uint32_t UART_THR = 0;
constexpr uint32_t UART_RBR = 0;
constexpr uint32_t UART_IER = 1;
constexpr uint32_t UART_FCR = 2;
constexpr uint32_t UART_LCR = 3;
constexpr uint32_t UART_MCR = 4;
constexpr uint32_t UART_LSR = 5;

constexpr uint32_t UART_LSR_DR = 0x01;
constexpr uint32_t UART_LSR_THRE = 0x20;

constexpr uint32_t UART_LCR_DLAB = 0x80;
constexpr uint32_t UART_LCR_8N1 = 0x03;

constexpr uint32_t UART_FCR_EN = 0x01;
constexpr uint32_t UART_FCR_CLR = 0x06;

void uart_init(void);
void uart_putc(int ch);
int uart_getc(void);

#endif

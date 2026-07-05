#ifndef _CUTEOS_ASM_TRAP_H
#define _CUTEOS_ASM_TRAP_H

/*
 * arch/riscv/include/asm/trap.h - RISC-V scause encodings
 */

#include <kernel/types.h>

/* ---- scause 中断/异常码 ----
 *
 * scause[63] = 1 表示中断, = 0 表示异常。
 * scause[62:0] 为具体编码。
 * 以下常量仅包含编码值，不含中断位。
 */

/* 中断码 (scause[63] = 1) */
#define IRQ_S_SOFT   1UL  /* Supervisor Software Interrupt */
#define IRQ_VS_SOFT  2UL  /* Virtual Supervisor Software Interrupt */
#define IRQ_M_SOFT   3UL  /* Machine Software Interrupt */
#define IRQ_S_TIMER  5UL  /* Supervisor Timer Interrupt */
#define IRQ_VS_TIMER 6UL  /* Virtual Supervisor Timer Interrupt */
#define IRQ_M_TIMER  7UL  /* Machine Timer Interrupt */
#define IRQ_S_EXT    9UL  /* Supervisor External Interrupt */
#define IRQ_VS_EXT   10UL /* Virtual Supervisor External Interrupt */
#define IRQ_M_EXT    11UL /* Machine External Interrupt */

/* 异常码 (scause[63] = 0) */
#define EXC_INST_MISALIGNED  0UL
#define EXC_INST_ACCESS	     1UL
#define EXC_INST_ILLEGAL     2UL
#define EXC_BREAKPOINT	     3UL
#define EXC_LOAD_MISALIGNED  4UL
#define EXC_LOAD_ACCESS	     5UL
#define EXC_STORE_MISALIGNED 6UL
#define EXC_STORE_ACCESS     7UL
#define EXC_ECALL_U	     8UL /* ecall from U-mode */
#define EXC_ECALL_S	     9UL /* ecall from S-mode */
#define EXC_INST_PAGE_FAULT  12UL
#define EXC_LOAD_PAGE_FAULT  13UL
#define EXC_STORE_PAGE_FAULT 15UL

/* scause 中断标志位 */
#define SCAUSE_IRQ_FLAG (1UL << 63)

#endif

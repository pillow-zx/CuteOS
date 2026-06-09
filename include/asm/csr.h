#ifndef _CUTEOS_ASM_CSR_H
#define _CUTEOS_ASM_CSR_H

/*
 * include/asm/csr.h - RISC-V CSR 寄存器地址和位操作定义
 *
 * 提供内联汇编宏用于读写 RISC-V 控制和状态寄存器，这些寄存器用于trap，
 * 页表和中断管理
 *
 * CSR addresses:
 *   sstatus   - Supervisor status register
 *   sepc      - Supervisor exception program counter
 *   scause    - Supervisor trap cause
 *   stval     - Supervisor trap value (bad address on faults)
 *   satp      - Supervisor address translation and protection (page table base)
 *   stvec     - Supervisor trap vector (base address + vectoring mode)
 *   sscratch  - Supervisor scratch (temp storage for trap entry)
 *   sie       - Supervisor interrupt enable
 *
 * Bit definitions:
 *   SSTATUS_SPP  - Supervisor Previous Privilege (1 = S-mode, 0 = U-mode)
 *   SSTATUS_SPIE - Supervisor Previous Interrupt Enable (sret restores SIE=SPIE)
 *   SSTATUS_SIE  - Supervisor Interrupt Enable
 *   SIE_STIE     - Supervisor Timer Interrupt Enable
 *   SIE_SEIE     - Supervisor External Interrupt Enable
 *   SATP_MODE_SV39 - SATP mode field for Sv39 paging (8 << 60)
 *
 * Inline asm macros:
 *   csr_read(csr)       - Read a CSR into a variable
 *   csr_write(csr, val) - Write a value to a CSR
 *   csr_set(csr, bits)  - Atomically set bits in a CSR
 *   csr_clear(csr, bits)- Atomically clear bits in a CSR
 */

#include <kernel/types.h>
#include <kernel/bitops.h>

#define SSTATUS_SPP  BIT(8) /* Supervisor Previous Privilege */
#define SSTATUS_SPIE BIT(5) /* Supervisor Previous Interrupt Enable */
#define SSTATUS_SIE  BIT(1)

#define SIE_STIE BIT(5)
#define SIE_SEIE BIT(9)

#define SATP_MODE_SV39 (8UL << 60)

#define csr_read(csr)                                                          \
	({                                                                     \
		size_t __v;                                                    \
		asm volatile("csrr %0, " #csr : "=r"(__v));                    \
		__v;                                                           \
	})

#define csr_write(csr, val) ({ asm volatile("csrw " #csr ", %0" ::"rK"(val)); })

#define csr_set(csr, bits) ({ asm volatile("csrs " #csr ", %0" ::"rK"(bits)); })

#define csr_clear(csr, bits)                                                   \
	({ asm volatile("csrc " #csr ", %0" ::"rK"(bits)); })

#define wfi() ({ asm volatile("wfi"); })

void sfence_vma_all(void);
void sfence_vma_addr(uintptr_t va);

#endif

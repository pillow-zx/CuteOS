#ifndef _CUTEOS_ASM_PAGE_H
#define _CUTEOS_ASM_PAGE_H

/*
 * arch/riscv/include/asm/page.h - RISC-V base page definitions
 *
 * This is the CPU/encoding layer.  It defines the architectural base page
 * size and PFN conversions only.  Platform memory layout and kernel virtual
 * mapping policy live in <arch/page.h>.
 */

#include <kernel/types.h>

/* 页大小 4 KiB，对应的位移量 */
#define PAGE_SIZE  4096
#define PAGE_SHIFT 12
#define PAGE_MASK  (~(PAGE_SIZE - 1))

/* 物理页号计算 */
#define PFN_DOWN(x)    ((x) >> PAGE_SHIFT)
#define PFN_UP(x)      (((x) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define PFN_PHYS(pfn)  ((uint64_t)(pfn) << PAGE_SHIFT)
#define PHYS_PFN(addr) ((addr) >> PAGE_SHIFT)

#endif

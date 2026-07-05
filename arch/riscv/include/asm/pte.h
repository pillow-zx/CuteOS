#ifndef _CUTEOS_ASM_PTE_H
#define _CUTEOS_ASM_PTE_H

/*
 * arch/riscv/include/asm/pte.h - RISC-V Sv39 页表项位定义与类型
 *
 * 根据 RISC-V 特权级规范定义 PTE 权限位、页表指针标志 PTE_TABLE，
 * 以及 pte_t 类型别名。
 *
 * Permission bits (bit position):
 *   PTE_V  = 1   (Valid)
 *   PTE_R  = 2   (Read)
 *   PTE_W  = 4   (Write)
 *   PTE_X  = 8   (Execute)
 *   PTE_U  = 16  (User)
 *   PTE_G  = 32  (Global)
 *   PTE_A  = 64  (Accessed)
 *   PTE_D  = 128 (Dirty)
 *
 * Derived flags:
 *   PTE_TABLE - Indicates a page table pointer (V=1, R=W=X=0)
 *
 * Types:
 *   pte_t - Page table entry type (uint64_t)
 */

#include <kernel/compiler.h>
#include <kernel/types.h>
#include <kernel/bitops.h>
#include <asm/page.h>

typedef uint64_t pte_t;

#define PTE_V BIT(0)
#define PTE_R BIT(1)
#define PTE_W BIT(2)
#define PTE_X BIT(3)
#define PTE_U BIT(4)
#define PTE_G BIT(5)
#define PTE_A BIT(6)
#define PTE_D BIT(7)

#define PTE_TABLE PTE_V

#define PTE_KERN_R   (PTE_V | PTE_R | PTE_G | PTE_A | PTE_D)
#define PTE_KERN_RX  (PTE_V | PTE_X | PTE_G | PTE_A | PTE_D)
#define PTE_KERN_RW  (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)
#define PTE_KERN_RWX (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)

#define PTE_USER_R   (PTE_V | PTE_R | PTE_U | PTE_A | PTE_D)
#define PTE_USER_RX  (PTE_V | PTE_R | PTE_X | PTE_U | PTE_A | PTE_D)
#define PTE_USER_RW  (PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D)
#define PTE_USER_RWX (PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D)

#define PTE_PPN_SHIFT	       10
#define PTE_PPN(pte)	       ((pte) >> PTE_PPN_SHIFT)
#define PTE_CREATE(pfn, flags) (((uint64_t)(pfn) << PTE_PPN_SHIFT) | (flags))
#define PTE_TO_PA(pte)	       ((uint64_t)PTE_PPN(pte) << PAGE_SHIFT)
#define PA_TO_PTE(pa)	       (PTE_CREATE(PHYS_PFN(pa), 0))

static __always_inline __must_check __pure bool asm_pte_present(pte_t pte)
{
	return (pte & PTE_V) != 0;
}

static __always_inline __must_check __pure bool asm_pte_user_page(pte_t pte)
{
	return (pte & PTE_U) != 0 && PTE_TO_PA(pte) != 0;
}

static __always_inline __must_check __pure bool asm_pte_leaf(pte_t pte)
{
	return (pte & (PTE_R | PTE_W | PTE_X)) != 0;
}

static __always_inline __must_check __pure paddr_t asm_pte_to_pa(pte_t pte)
{
	return PTE_TO_PA(pte);
}

#endif

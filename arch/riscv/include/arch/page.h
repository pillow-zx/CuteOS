#ifndef _CUTEOS_ARCH_RISCV_PAGE_H
#define _CUTEOS_ARCH_RISCV_PAGE_H

#include <asm/page.h>
#include <kernel/compiler.h>
#include <kernel/types.h>

/*
 * arch/riscv/include/arch/page.h - RISC-V platform address layout
 *
 * This layer builds on <asm/page.h>.  It owns QEMU virt DRAM layout, the
 * kernel direct map, and user virtual address policy exposed to generic MM.
 */

#define DRAM_BASE 0x80000000
#define DRAM_SIZE ((uint64_t)CONFIG_DRAM_SIZE_MB << 20)

#define TASK_SIZE	      0x80000000UL
#define USER_STACK_TOP	      TASK_SIZE
#define USER_STACK_BASE	      (USER_STACK_TOP - PAGE_SIZE)
#define USER_STACK_GUARD_BASE (USER_STACK_BASE - PAGE_SIZE)

#define KERNEL_VBASE 0xFFFFFFC000000000

static __always_inline __must_check __const paddr_t arch_pa(uintptr_t x)
{
	return (vaddr_t)x - KERNEL_VBASE;
}

static __always_inline __must_check __const void *__va_const(paddr_t x)
{
	return (void *)(x + KERNEL_VBASE);
}

static __always_inline __must_check __const void *__va(paddr_t x)
{
	return (void *)__va_const(x);
}

#define __pa(x) arch_pa((uintptr_t)(x))
#define __va(x) ((void *)__va_const((paddr_t)(x)))

void pagetable_init(void);
void *__must_check arch_bootmem_end(void);

#endif

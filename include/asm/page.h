#ifndef _CUTEOS_ASM_PAGE_H
#define _CUTEOS_ASM_PAGE_H

/*
 * page.h - 内存页面与物理/虚拟地址转换定义
 *
 * 定义页大小、DRAM 物理地址范围、内核虚拟地址基址，
 * 以及 __pa/__va 用于物理地址与内核虚拟地址之间的转换。
 */

#include <kernel/types.h>

/* 页大小 4 KiB，对应的位移量 */
#define PAGE_SIZE	        4096
#define PAGE_SHIFT	        12
#define PAGE_MASK	        (~(PAGE_SIZE - 1))

/* 物理页号计算 */
#define PFN_DOWN(x)             ((x) >> PAGE_SHIFT)
#define PFN_UP(x)               (((x) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define PFN_PHYS(pfn)           ((uint64_t)(pfn) << PAGE_SHIFT)
#define PHYS_PFN(addr)          ((addr) >> PAGE_SHIFT)

/* 物理内存 (DRAM) 起始地址与大小 */
#define DRAM_BASE               0x80000000
#define DRAM_SIZE               0x10000000

/* 用户进程地址空间上限 (1 GiB) */
#define TASK_SIZE               0x40000000

/* 内核直接映射虚拟地址基址 (高地址区) */
#define KERNEL_VBASE            0xFFFFFFC000000000

/*
 * __pa - 将内核虚拟地址转换为物理地址
 * @x: 内核虚拟地址
 *
 * 仅适用于通过 KERNEL_VBASE 直接映射的地址，
 * 不适用于 vmalloc 分配的地址或用户空间地址。
 */
#define __pa(x) ((uintptr_t)(x) - KERNEL_VBASE)

/*
 * __va - 将物理地址转换为内核虚拟地址
 * @x: 物理地址
 *
 * 依赖内核直接映射关系，物理地址加上 KERNEL_VBASE
 * 即可得到对应的内核虚拟地址。
 */
#define __va(x) ((void *)((uintptr_t)(x) + KERNEL_VBASE))

/*
 * kernel_pagetable_init - 初始化正式内核页表并切换 satp
 *
 * 使用 4KB 普通页映射 256MB DRAM（高地址 + 恒等映射），
 * 使用 1GB mega page 映射 MMIO，然后切换到新页表。
 * 必须在 buddy_init 之前调用。
 */
void kernel_pagetable_init(void);

/*
 * page_table_mem_end - 返回早期页表分配结束后的地址
 *
 * buddy_init() 调用此函数确定空闲内存起始位置。
 */
void *page_table_mem_end(void);

#endif

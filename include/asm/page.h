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
#define PAGE_SIZE		4096
#define PAGE_SHIFT		12

/* 物理内存 (DRAM) 起始地址与大小 */
#define DRAM_BASE		0x80000000
#define DRAM_SIZE		0x10000000

/* 用户进程地址空间上限 (1 GiB) */
#define TASK_SIZE		0x40000000

/* 内核直接映射虚拟地址基址 (高地址区) */
#define KERNEL_VBASE		0xFFFFFFC000000000

/*
 * __pa - 将内核虚拟地址转换为物理地址
 * @x: 内核虚拟地址
 *
 * 仅适用于通过 KERNEL_VBASE 直接映射的地址，
 * 不适用于 vmalloc 分配的地址或用户空间地址。
 */
#define __pa(x)		((uintptr_t)(x) - KERNEL_VBASE)

/*
 * __va - 将物理地址转换为内核虚拟地址
 * @x: 物理地址
 *
 * 依赖内核直接映射关系，物理地址加上 KERNEL_VBASE
 * 即可得到对应的内核虚拟地址。
 */
#define __va(x)		((void *)((uintptr_t)(x) + KERNEL_VBASE))

#endif

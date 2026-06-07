#ifndef _CUTEOS_KERNEL_BUDDY_H
#define _CUTEOS_KERNEL_BUDDY_H

/*
 * include/kernel/buddy.h - 物理页分配器（buddy system）
 *
 * 声明 buddy system 接口，用于管理物理内存。分配器维护若干空闲链表，
 * 每条链表包含 2 的幂次方个连续页块，最大阶数为 MAX_ORDER。
 *
 * Constants:
 *   MAX_ORDER = 9    (max allocation: 2^9 = 512 pages = 2 MB)
 *   NR_PAGES          (total physical pages = DRAM_SIZE / PAGE_SIZE)
 *
 * struct page - Forward declaration; full definition in kernel/page.h
 *
 * Functions:
 *   get_free_page(order) - Allocate 2^order contiguous physical pages,
 *                          returns kernel virtual address
 *   free_page(addr, order) - Free a block previously allocated at addr
 */

#endif

/*
 * include/kernel/buddy.h - 物理页分配器（buddy system）
 *
 * 声明 buddy system 接口，用于管理物理内存。分配器维护若干空闲链表，
 * 每条链表包含 2 的幂次方个连续页块，最大阶数为 MAX_ORDER。
 *
 * Constants:
 *   MAX_ORDER = 9    (max allocation: 2^9 = 512 pages = 2 MB)
 *
 * struct free_area - 每阶一个，含空闲链表和计数器
 *
 * Functions:
 *   buddy_init()                 - 从 page_table_mem_end() 到 DRAM 末尾初始化
 *   get_free_page(order)         - 分配 2^order 连续物理页，返回内核虚拟地址
 *   free_page(addr, order)       - 释放指定地址的页块，尝试伙伴合并
 */

#ifndef _CUTEOS_KERNEL_BUDDY_H
#define _CUTEOS_KERNEL_BUDDY_H

#include <kernel/types.h>
#include <kernel/page.h>
#include <kernel/list.h>

#define MAX_ORDER 9 /* 最大分配阶：2^9 = 512 页 = 2 MB */

/**
 * struct free_area - 某一阶的空闲页块集合
 * @free_list: 空闲页块链表（每个块由首页 struct page 的 lru 挂入）
 * @nr_free:   当前阶的空闲块数量
 */
struct free_area {
	struct list_head free_list;
	uint32_t nr_free;
};

/* 全局 mem_map：每个物理页帧对应一个 struct page */
extern struct page *mem_map;

/* 各阶空闲链表 */
extern struct free_area free_area[];

/**
 * buddy_init - 初始化伙伴系统
 *
 * 在 page_table_mem_end() 之后放置 mem_map 数组，然后将可用页
 * 按最大可能的连续块加入空闲链表。内核映像和页表占用的页标记为
 * PG_reserved。
 */
void buddy_init(void);

/**
 * get_free_page - 分配 2^order 个连续物理页
 * @order: 分配阶数（0 = 1 页, ..., MAX_ORDER = 512 页）
 *
 * 返回首页的内核虚拟地址，分配失败（OOM）返回 NULL。
 */
void *get_free_page(uint32_t order);

/**
 * free_page - 释放页块并尝试伙伴合并
 * @addr:  get_free_page 返回的内核虚拟地址
 * @order: 释放的阶数，须与分配时一致
 */
void free_page(void *addr, uint32_t order);

size_t buddy_free_pages(void);

#endif

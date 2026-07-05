/*
 * include/kernel/page.h - 物理页描述符
 *
 * 功能：
 *   定义 struct page，系统中每个物理页帧对应一个实例。全局数组 mem_map
 *   保存所有 struct page 条目，由 buddy 分配器管理分配与释放。
 *
 * struct page 字段：
 *   flags    - 页状态标志位（PG_RESERVED, PG_SLAB, PG_BUDDY 等）
 *   order    - 在 buddy 空闲链表中的分配阶数
 *   refcount - 引用计数
 *   lru      - 链表节点，用于 buddy 空闲链表链接
 *
 * 页标志：
 *   PG_RESERVED - 保留给内核使用（不可分配）
 *   PG_SLAB     - 页由 SLAB 分配器管理
 *   PG_BUDDY    - 页块当前挂在 buddy 空闲链表中
 */

#ifndef _CUTEOS_KERNEL_PAGE_H
#define _CUTEOS_KERNEL_PAGE_H

#include <kernel/types.h>
#include <kernel/list.h>
#include <kernel/bitops.h>
#include <kernel/compiler.h>
#include <arch/page.h>

/* 页标志位定义 */
#define PG_RESERVED 0
#define PG_SLAB	    1
#define PG_BUDDY    2

/**
 * struct page - 物理页帧描述符
 *
 * 全局数组 mem_map[] 中每个页帧对应一个实例。buddy 分配器通过
 * lru 将空闲页块串入 free_area[order] 链表。
 */
struct page {
	uint32_t flags;
	uint32_t order;
	uint32_t refcount;
	struct list_head lru;
};

static __always_inline __must_check __pure
	__nonnull(1) bool page_test_flag(const struct page *page, uint32_t flag)
{
	return test_bit(page->flags, flag);
}

static __always_inline __nonnull(1) void page_set_flag(struct page *page,
						       uint32_t flag)
{
	set_bit(page->flags, flag);
}

static __always_inline __nonnull(1) void page_clear_flag(struct page *page,
							 uint32_t flag)
{
	clr_bit(page->flags, flag);
}

static __always_inline __must_check void *bootmem_end(void)
{
	return arch_bootmem_end();
}

#endif

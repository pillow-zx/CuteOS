#ifndef _CUTEOS_KERNEL_PAGE_H
#define _CUTEOS_KERNEL_PAGE_H

/*
 * include/kernel/page.h - 物理页描述符
 *
 * 定义 struct page，系统中每个物理页帧对应一个实例。全局数组 mem_map
 * 保存所有 struct page 条目，由 buddy 分配器管理分配与释放。
 *
 * struct page fields:
 *   flags    - Page status flags (PG_reserved, PG_slab, etc.)
 *   order    - Allocation order if in the buddy free lists
 *   refcount - Reference count
 *   lru      - List head for buddy free-list or LRU linkage
 *
 * Page flags:
 *   PG_reserved - Reserved for kernel use (not allocatable)
 *   PG_slab     - Page is owned by the SLAB allocator
 */

#endif

/**
 * @file buddy.h
 * @brief 物理页 buddy 分配器接口。
 */

#ifndef _CUTEOS_KERNEL_BUDDY_H
#define _CUTEOS_KERNEL_BUDDY_H

#include <kernel/types.h>
#include <kernel/page.h>
#include <kernel/list.h>
#include <kernel/cleanup.h>
#include <kernel/compiler.h>

/**
 * @def MAX_ORDER
 * @brief Highest buddy order managed by the physical page allocator.
 */
constexpr uint32_t MAX_ORDER = 9;

/**
 * @struct free_area
 * @brief Free-list bucket for one buddy order.
 *
 * @par Fields
 * - @c free_list: Free block heads for this order.
 * - @c nr_free: Number of free blocks in @ref free_list.
 */
struct free_area {
	struct list_head free_list;
	uint32_t nr_free;
};

/**
 * @brief Global physical page descriptor array.
 */
extern struct page *mem_map;

/**
 * @brief Per-order free lists.
 */
extern struct free_area free_area[];

/**
 * @brief Initialize mem_map and buddy free lists from the boot memory range.
 */
void buddy_init(void);

/**
 * @brief Allocate a physically contiguous power-of-two page block.
 * @param order Buddy order; allocation size is PAGE_SIZE << order.
 * @return Direct-map virtual address of the block, or NULL.
 */
void *__must_check __malloc get_free_page(uint32_t order);


/**
 * @brief Free a block allocated by get_free_page().
 * @param addr Direct-map virtual address of the block.
 * @param order Buddy order used for the allocation.
 */
void __nonnull(1) free_page(void *addr, uint32_t order);


CLEANUP_DEFINE(page0, char *, if (_T) free_page(_T, 0));

size_t __must_check __pure buddy_free_pages(void);
struct page *__must_check __pure virt_to_page(const void *addr);
void *__must_check __pure __nonnull(1) page_to_virt(const struct page *page);

#ifdef KERNEL_SELFTEST
void buddy_test_validate(void);
#endif

#endif

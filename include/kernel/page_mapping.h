#ifndef _CUTEOS_KERNEL_PAGE_MAPPING_H
#define _CUTEOS_KERNEL_PAGE_MAPPING_H

/**
 * @file page_mapping.h
 * @brief page cache 后端映射抽象。
 */

#include <kernel/list.h>
#include <kernel/types.h>

/**
 * @struct page_mapping
 * @brief Identity and backend operations for cached 4 KiB pages.
 *
 * @par Fields
 * - @c host: Owning inode, block device, or mapping-specific object.
 * - @c ops: Backend I/O and block-map hooks.
 * - @c backing: Lower raw-block mapping for aliases.
 * - @c pages: Cached pages owned by this mapping.
 * - @c dirty_pages: Dirty pages pending writeback.
 */
struct page_mapping {
	void *host;
	const struct page_mapping_ops *ops;
	struct page_mapping *backing;
	struct list_head pages;
	struct list_head dirty_pages;
};

/**
 * @struct page_mapping_ops
 * @brief Backend operations used by the page-cache core.
 *
 * @par Fields
 * - @c readpage: Read one 4 KiB page at @p index into @p data.
 * - @c map_block: Resolve a logical page index to a filesystem/device block.
 * - @c writepages: Write a contiguous run of 4 KiB pages starting at @p start_index.
 */
struct page_mapping_ops {
	int (*readpage)(struct page_mapping *mapping, uint64_t index,
			void *data);
	int (*map_block)(struct page_mapping *mapping, uint64_t index,
			 bool create, uint32_t *block);
	int (*writepages)(struct page_mapping *mapping, uint64_t start_index,
			  uint32_t nr_pages, const void *data);
};

/**
 * @brief Initialize a page-cache mapping object.
 * @param mapping Mapping object to initialize.
 * @param host Owner object used by mapping operations.
 * @param ops Backend operations.
 * @param backing Optional lower mapping used for alias consistency.
 */
static __always_inline void
page_mapping_init(struct page_mapping *mapping, void *host,
		  const struct page_mapping_ops *ops,
		  struct page_mapping *backing)
{
	if (!mapping)
		return;

	mapping->host = host;
	mapping->ops = ops;
	mapping->backing = backing;
	INIT_LIST_HEAD(&mapping->pages);
	INIT_LIST_HEAD(&mapping->dirty_pages);
}

#endif

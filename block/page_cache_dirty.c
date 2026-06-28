/*
 * block/page_cache_dirty.c - page cache dirty-list management
 */

#include "page_cache_internal.h"

#include <kernel/list.h>

static LIST_HEAD(page_cache_dirty_pages);

void page_cache_remove_dirty(struct page_cache *page)
{
	if (!page)
		return;

	/*
	 * Dirty membership is duplicated: the global list drives background
	 * writeback, and the mapping list drives fsync/truncate-scoped work.
	 * Always update both lists together.
	 */
	if (!list_empty(&page->dirty_node))
		list_del_init(&page->dirty_node);
	if (!list_empty(&page->mapping_dirty_node))
		list_del_init(&page->mapping_dirty_node);
	page->dirty = false;
}

bool page_cache_is_dirty(const struct page_cache *page)
{
	return page && page->dirty;
}

void page_cache_mark_dirty(struct page_cache *page)
{
	if (!page || !page->owner || page->dropped)
		return;

	/*
	 * Marking dirty also asserts the cache has the newest bytes.  A dirty
	 * non-uptodate page would make writeback copy undefined data to disk.
	 */
	if (!page->dirty) {
		list_add_tail(&page->dirty_node, &page_cache_dirty_pages);
		list_add_tail(&page->mapping_dirty_node,
			      &page->owner->dirty_pages);
	}

	page->dirty = true;
	page->uptodate = true;
}

struct page_cache *page_cache_first_dirty_mapping(struct page_mapping *mapping)
{
	if (!mapping || list_empty(&mapping->dirty_pages))
		return NULL;

	return list_first_entry(&mapping->dirty_pages, struct page_cache,
				mapping_dirty_node);
}

struct page_cache *page_cache_first_dirty_global(void)
{
	if (list_empty(&page_cache_dirty_pages))
		return NULL;

	return list_first_entry(&page_cache_dirty_pages, struct page_cache,
				dirty_node);
}

/*
 * block/page_cache_alias.c - page cache block-alias coherency policy
 */

#include "page_cache_internal.h"

#include <kernel/blkdev.h>

void page_cache_alias_refresh(struct page_mapping *mapping, uint32_t blocknr,
			      const void *data)
{
	struct page_mapping *backing;
	struct page_cache *alias;

	if (!mapping || !data)
		return;

	backing = mapping->backing;
	if (!backing)
		return;

	alias = page_cache_find(backing, blocknr);
	if (!alias)
		return;


	memcpy(alias->data, data, BLOCK_SIZE);
	alias->uptodate = true;
	page_cache_clear_dirty(alias);
}

void page_cache_alias_invalidate(struct page_cache *page)
{
	struct page_mapping *mapping;
	struct page_mapping *backing;
	struct page_cache *alias;
	uint32_t blocknr;

	if (!page || !page->owner)
		return;

	mapping = page->owner;
	backing = mapping->backing;
	if (!backing || !mapping->ops || !mapping->ops->map_block)
		return;
	if (mapping->ops->map_block(mapping, page->index, false, &blocknr) < 0)
		return;

	alias = page_cache_find(backing, blocknr);
	if (!alias)
		return;


	alias->uptodate = false;
	page_cache_clear_dirty(alias);
}

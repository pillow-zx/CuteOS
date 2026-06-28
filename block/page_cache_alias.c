/*
 * block/page_cache_alias.c - page cache block-alias coherency policy
 *
 * Inode mappings are authoritative for file, directory, and block-backed
 * symlink data.  A backing block-device mapping is only a raw physical-block
 * view.  This module keeps resident raw aliases coherent without creating new
 * reverse indexes or changing I/O routing.
 */

#include "page_cache_internal.h"

#include <kernel/blkdev.h>
#include <kernel/string.h>

void page_cache_alias_refresh_after_writeback(struct page_mapping *mapping,
					      uint32_t blocknr,
					      const void *data)
{
	struct page_mapping *block_mapping;
	struct page_cache *alias;

	if (!mapping || !data)
		return;

	block_mapping = mapping->backing;
	if (!block_mapping)
		return;

	alias = page_cache_find(block_mapping, blocknr);
	if (!alias)
		return;

	/*
	 * The authoritative page has reached disk.  A cached raw-block alias can
	 * now safely mirror those bytes and become clean.
	 */
	memcpy(alias->data, data, BLOCK_SIZE);
	alias->uptodate = true;
	page_cache_remove_dirty(alias);
}

void page_cache_alias_invalidate_after_drop(struct page_cache *page)
{
	struct page_mapping *mapping;
	struct page_mapping *block_mapping;
	struct page_cache *alias;
	uint32_t blocknr;

	if (!page || !page->owner)
		return;

	mapping = page->owner;
	block_mapping = mapping->backing;
	if (!block_mapping || !mapping->ops || !mapping->ops->map_block)
		return;
	if (mapping->ops->map_block(mapping, page->index, false, &blocknr) < 0)
		return;

	alias = page_cache_find(block_mapping, blocknr);
	if (!alias)
		return;

	/*
	 * The upper mapping is explicitly dropping its authoritative copy.  Keep
	 * the raw alias object resident if it exists, but force the next raw read
	 * to fetch from the block device.
	 */
	alias->uptodate = false;
	page_cache_remove_dirty(alias);
}

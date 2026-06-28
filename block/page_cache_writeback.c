/*
 * block/page_cache_writeback.c - page cache sync and writeback clustering
 */

#include "page_cache_internal.h"

#include <kernel/blkdev.h>
#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <kernel/string.h>
#include <kernel/timer.h>

#define PAGE_CACHE_WB_ORDER 5U

static uint8_t *page_cache_wb_buf;
static uint32_t page_cache_wb_pages = 1;
static bool page_cache_wb_ready;

void page_cache_writeback_init_once(void)
{
	if (page_cache_wb_ready)
		return;

	page_cache_wb_buf = get_free_page(PAGE_CACHE_WB_ORDER);
	if (page_cache_wb_buf)
		page_cache_wb_pages = 1u << PAGE_CACHE_WB_ORDER;
	else {
		page_cache_wb_buf = get_free_page(0);
		page_cache_wb_pages = page_cache_wb_buf ? 1u : 0u;
	}

	page_cache_wb_ready = true;
}

static void page_cache_sleep_until(uint64_t deadline)
{
	(void)timer_sleep_until(deadline, false);
}

static void page_cache_refresh_block_alias(struct page_mapping *mapping,
					   uint32_t blocknr,
					   const void *data)
{
	struct page_mapping *block_mapping;
	struct page_cache *alias;

	if (!mapping || !data)
		return;

	/*
	 * Only mappings with a lower block-device mapping can have aliases.
	 * A raw block-device mapping has no backing and therefore nothing to
	 * refresh.
	 */
	block_mapping = mapping->backing;
	if (!block_mapping)
		return;

	alias = page_cache_find(block_mapping, blocknr);
	if (!alias)
		return;

	/*
	 * The just-written higher-level page is now the freshest copy.  The
	 * block alias must become clean because its bytes are already on disk.
	 */
	memcpy(alias->data, data, BLOCK_SIZE);
	alias->uptodate = true;
	page_cache_remove_dirty(alias);
}

int page_cache_sync_page(struct page_cache *page)
{
	struct page_mapping *mapping;
	uint32_t blocknr;
	int ret;

	if (!page || !page->owner)
		return -EINVAL;

	mapping = page->owner;
	if (!mapping->ops || !mapping->ops->writepages)
		return -EINVAL;

	page->writeback = true;
	ret = mapping->ops->writepages(mapping, page->index, 1, page->data);
	page->writeback = false;
	if (ret == 0) {
		/*
		 * The page's own mapping is authoritative after writeback.
		 * If it also names a physical block through backing, update the
		 * raw block cache alias so later metadata reads see the same
		 * bytes without forcing an invalidate.
		 */
		page_cache_remove_dirty(page);
		page->uptodate = true;
		if (mapping->ops->map_block &&
		    mapping->ops->map_block(mapping, page->index, false,
					    &blocknr) == 0)
			page_cache_refresh_block_alias(mapping, blocknr,
						       page->data);
	}
	return ret;
}

int page_cache_sync_block(struct page_cache *page)
{
	return page_cache_sync_page(page);
}

int page_cache_writeback_run(struct page_cache *start)
{
	struct page_mapping *mapping;
	struct page_cache *pages[32];
	uint32_t pblocks[32];
	uint32_t nr_pages = 0;
	uint32_t run_pages;
	uint64_t start_index;
	uint32_t prev_block;
	int ret;

	if (!start || !start->owner || !start->dirty)
		return -EINVAL;
	mapping = start->owner;
	if (!mapping->ops || !mapping->ops->map_block ||
	    !mapping->ops->writepages)
		return -EINVAL;

	page_cache_writeback_init_once();
	if (!page_cache_wb_buf || page_cache_wb_pages == 0)
		return -ENOMEM;

	/*
	 * Clustered writeback is conservative: collect only dirty pages from the
	 * same mapping whose logical indexes are adjacent and whose physical
	 * blocks are adjacent.  That lets writepages() issue one contiguous
	 * device write without requiring the filesystem to handle scatter/gather.
	 */
	start_index = start->index;
	run_pages = page_cache_wb_pages;
	if (run_pages > 32u)
		run_pages = 32u;

	ret = mapping->ops->map_block(mapping, start_index, false,
				      &prev_block);
	if (ret < 0)
		return ret;

	pblocks[0] = prev_block;
	pages[nr_pages++] = start;
	while (nr_pages < run_pages) {
		struct page_cache *next;
		uint32_t next_block;
		uint64_t next_index = start_index + nr_pages;

		next = page_cache_find(mapping, next_index);
		if (!next || !next->dirty || next->writeback)
			break;

		ret = mapping->ops->map_block(mapping, next_index, false,
					      &next_block);
		if (ret < 0 || next_block != prev_block + 1)
			break;

		pblocks[nr_pages] = next_block;
		pages[nr_pages++] = next;
		prev_block = next_block;
	}

	for (uint32_t i = 0; i < nr_pages; i++) {
		pages[i]->writeback = true;
		memcpy(page_cache_wb_buf + i * BLOCK_SIZE, pages[i]->data,
		       BLOCK_SIZE);
	}

	ret = mapping->ops->writepages(mapping, start_index, nr_pages,
				       page_cache_wb_buf);
	for (uint32_t i = 0; i < nr_pages; i++) {
		pages[i]->writeback = false;
		if (ret == 0) {
			page_cache_refresh_block_alias(mapping, pblocks[i],
						       pages[i]->data);
			page_cache_remove_dirty(pages[i]);
		}
	}

	return ret;
}

int page_cache_writeback_mapping(struct page_mapping *mapping)
{
	int ret = 0;

	if (!mapping)
		return -EINVAL;

	for (;;) {
		struct page_cache *page =
			page_cache_first_dirty_mapping(mapping);

		if (!page)
			break;

		ret = page_cache_writeback_run(page);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int page_cache_writeback_inode(struct inode *inode)
{
	if (!inode)
		return -EINVAL;

	return page_cache_writeback_mapping(&inode->i_pages);
}

int page_cache_writeback_all(void)
{
	int ret = 0;

	for (;;) {
		struct page_cache *page = page_cache_first_dirty_global();

		if (!page)
			break;

		ret = page_cache_writeback_run(page);
		if (ret < 0)
			return ret;
	}

	return 0;
}

void page_cache_writeback_thread(void *arg)
{
	(void)arg;

	for (;;) {
		uint64_t deadline = arch_timer_now() + 5 * MTIME_FREQ;

		page_cache_sleep_until(deadline);
		(void)page_cache_writeback_all();
	}
}

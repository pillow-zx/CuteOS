/*
 * block/page_cache_writeback.c - page cache sync and writeback clustering
 */

#include "page_cache_internal.h"

#include <kernel/blkdev.h>
#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <kernel/worker.h>

#define PAGE_CACHE_WB_ORDER 5U

static uint8_t *wb_buf;
static uint32_t wb_pages = 1;
static bool wb_ready;

void page_cache_wb_init(void)
{
	if (wb_ready)
		return;

	wb_buf = get_free_page(PAGE_CACHE_WB_ORDER);
	if (wb_buf)
		wb_pages = 1u << PAGE_CACHE_WB_ORDER;
	else {
		wb_buf = get_free_page(0);
		wb_pages = wb_buf ? 1u : 0u;
	}

	wb_ready = true;
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
		page_cache_clear_dirty(page);
		page->uptodate = true;
		if (mapping->ops->map_block &&
		    mapping->ops->map_block(mapping, page->index, false,
					    &blocknr) == 0)
			page_cache_alias_refresh(mapping, blocknr, page->data);
	}
	return ret;
}

int page_cache_sync_block(struct page_cache *page)
{
	return page_cache_sync_page(page);
}

int page_cache_wb_run(struct page_cache *start)
{
	struct page_mapping *mapping;
	struct page_cache *pages[32];
	uint32_t pblocks[32];
	uint32_t nr = 0;
	uint32_t limit;
	uint64_t index;
	uint32_t prev;
	int ret;

	if (!start || !start->owner || !start->dirty)
		return -EINVAL;
	mapping = start->owner;
	if (!mapping->ops || !mapping->ops->map_block ||
	    !mapping->ops->writepages)
		return -EINVAL;

	page_cache_wb_init();
	if (!wb_buf || wb_pages == 0)
		return -ENOMEM;

	/*
	 * Clustered writeback is conservative: collect only dirty pages from
	 * the same mapping whose logical indexes are adjacent and whose
	 * physical blocks are adjacent.  That lets writepages() issue one
	 * contiguous device write without requiring the filesystem to handle
	 * scatter/gather.
	 */
	index = start->index;
	limit = wb_pages;
	if (limit > 32u)
		limit = 32u;

	ret = mapping->ops->map_block(mapping, index, false, &prev);
	if (ret < 0)
		return ret;

	pblocks[0] = prev;
	pages[nr++] = start;
	while (nr < limit) {
		struct page_cache *next;
		uint32_t block;
		uint64_t next_index = index + nr;

		next = page_cache_find(mapping, next_index);
		if (!next || !next->dirty || next->writeback)
			break;

		ret = mapping->ops->map_block(mapping, next_index, false,
					      &block);
		if (ret < 0 || block != prev + 1)
			break;

		pblocks[nr] = block;
		pages[nr++] = next;
		prev = block;
	}

	for (uint32_t i = 0; i < nr; i++) {
		pages[i]->writeback = true;
		memcpy(wb_buf + i * BLOCK_SIZE, pages[i]->data, BLOCK_SIZE);
	}

	ret = mapping->ops->writepages(mapping, index, nr, wb_buf);
	for (uint32_t i = 0; i < nr; i++) {
		pages[i]->writeback = false;
		if (ret == 0) {
			page_cache_alias_refresh(mapping, pblocks[i],
						 pages[i]->data);
			page_cache_clear_dirty(pages[i]);
		}
	}

	return ret;
}

int page_cache_sync_mapping(struct page_mapping *mapping)
{
	int ret = 0;

	if (!mapping)
		return -EINVAL;

	for (;;) {
		struct page_cache *page = page_cache_dirty_first(mapping);

		if (!page)
			break;

		ret = page_cache_wb_run(page);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int page_cache_sync_inode(struct inode *inode)
{
	if (!inode)
		return -EINVAL;

	return page_cache_sync_mapping(&inode->i_pages);
}

int page_cache_sync_all(void)
{
	int ret = 0;

	for (;;) {
		struct page_cache *page = page_cache_dirty_any();

		if (!page)
			break;

		ret = page_cache_wb_run(page);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void page_cache_wb_once(void *arg)
{
	(void)arg;
	(void)page_cache_sync_all();
}

void page_cache_wb_thread(void *arg)
{
	worker_run_periodic(5, page_cache_wb_once, arg);
}

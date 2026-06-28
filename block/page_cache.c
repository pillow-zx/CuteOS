/*
 * block/page_cache.c - unified 4 KiB page cache
 *
 * The cache key is (struct page_mapping *, index).  This is deliberately
 * more general than the old split between "metadata block" and "file data":
 * block devices own a mapping whose indexes are physical disk blocks, while
 * inodes own mappings whose indexes are file logical blocks.  The page cache
 * stores and writes back pages without embedding ext2-specific knowledge.
 *
 * Some disk blocks can be visible through two names at once: as an inode page
 * (logical block N of a file) and as a raw block-device page (physical block
 * P).  page_mapping::backing records that relationship so successful inode
 * writeback can refresh an already-cached raw block alias.
 */

#include "page_cache_internal.h"

#include <kernel/blkdev.h>
#include <kernel/buddy.h>
#include <kernel/fs.h>
#include <kernel/hash.h>
#include <kernel/list.h>
#include <kernel/slab.h>
#include <kernel/string.h>

#define PAGE_CACHE_HASH_BITS 7
#define PAGE_CACHE_NR_PAGES  512U

HASH_TABLE_DECLARE_STATIC(page_cache_hashtable, PAGE_CACHE_HASH_BITS);
static LIST_HEAD(page_cache_lru);
static uint32_t page_cache_pages;
static bool page_cache_ready;

void page_cache_init_once(void)
{
	if (page_cache_ready)
		return;

	hash_table_init(&page_cache_hashtable);
	INIT_LIST_HEAD(&page_cache_lru);
	page_cache_wb_init();

	page_cache_ready = true;
}

static uint32_t page_cache_hash(struct page_mapping *mapping, uint64_t index)
{
	uintptr_t key = (uintptr_t)mapping;

	/*
	 * mapping addresses are stable for the lifetime of their owner.  Mixing
	 * the pointer with the index keeps file logical block 0 distinct from
	 * physical disk block 0 and from every other inode's block 0.
	 */
	return (uint32_t)(key ^ index ^ (index >> PAGE_CACHE_HASH_BITS));
}

struct page_cache *page_cache_find(struct page_mapping *mapping,
				   uint64_t index)
{
	struct list_head *pos;
	uint32_t hash = page_cache_hash(mapping, index);

	hash_table_for_each_possible (pos, &page_cache_hashtable, hash) {
		struct page_cache *page =
			list_entry(pos, struct page_cache, hash_node);

		if (page->owner == mapping && page->index == index)
			return page;
	}

	return NULL;
}

static void page_cache_free_page(struct page_cache *page)
{
	if (!page)
		return;

	page_cache_clear_dirty(page);
	if (!list_empty(&page->hash_node))
		list_del_init(&page->hash_node);
	if (!list_empty(&page->lru_node))
		list_del_init(&page->lru_node);
	if (!list_empty(&page->mapping_node))
		list_del_init(&page->mapping_node);

	if (page->data)
		free_page(page->data, 0);
	kfree(page);
	if (page_cache_pages > 0)
		page_cache_pages--;
}

static void page_cache_drop_page(struct page_cache *page)
{
	if (!page)
		return;

	/*
	 * Drop removes the page from all discoverable structures immediately.
	 * A caller may still be using page->data, so actual memory release waits
	 * until the reference count reaches zero.
	 */
	page_cache_alias_invalidate(page);
	page_cache_clear_dirty(page);
	page->writeback = false;
	page->uptodate = false;
	page->dropped = true;
	page->owner = NULL;
	if (!list_empty(&page->hash_node))
		list_del_init(&page->hash_node);
	if (!list_empty(&page->lru_node))
		list_del_init(&page->lru_node);
	if (!list_empty(&page->mapping_node))
		list_del_init(&page->mapping_node);
	memset(page->data, 0, BLOCK_SIZE);

	if (page->refcount == 0)
		page_cache_free_page(page);
}

static bool page_cache_evict_one_clean(void)
{
	struct list_head *pos;
	struct list_head *next;

	list_for_each_safe (pos, next, &page_cache_lru) {
		struct page_cache *page =
			list_entry(pos, struct page_cache, lru_node);

		if (page->refcount != 0 || page->dirty || page->writeback)
			continue;
		page_cache_free_page(page);
		return true;
	}

	return false;
}

static bool page_cache_flush_one_victim(void)
{
	struct list_head *pos;
	struct list_head *next;

	/*
	 * If the cache is full and no clean page can be evicted, write one dirty
	 * unreferenced page first.  wb_run() may also flush adjacent pages,
	 * after which a clean victim should be available.
	 */
	list_for_each_safe (pos, next, &page_cache_lru) {
		struct page_cache *page =
			list_entry(pos, struct page_cache, lru_node);

		if (page->refcount != 0 || !page->dirty || page->writeback)
			continue;
		if (page_cache_wb_run(page) < 0)
			return false;
		return page_cache_evict_one_clean();
	}

	return false;
}

static bool page_cache_ensure_space(void)
{
	page_cache_init_once();

	while (page_cache_pages >= PAGE_CACHE_NR_PAGES) {
		if (page_cache_evict_one_clean())
			continue;
		if (!page_cache_flush_one_victim())
			return false;
	}

	return true;
}

static struct page_cache *page_cache_alloc_page(void)
{
	struct page_cache *page;

	if (!page_cache_ensure_space())
		return NULL;

	page = kmalloc(sizeof(*page));
	if (!page)
		return NULL;
	memset(page, 0, sizeof(*page));

	page->data = get_free_page(0);
	if (!page->data) {
		kfree(page);
		return NULL;
	}
	memset(page->data, 0, BLOCK_SIZE);

	INIT_LIST_HEAD(&page->hash_node);
	INIT_LIST_HEAD(&page->lru_node);
	INIT_LIST_HEAD(&page->mapping_node);
	INIT_LIST_HEAD(&page->dirty_node);
	INIT_LIST_HEAD(&page->dirty_map_node);
	page_cache_pages++;

	return page;
}

static void page_cache_add_page(struct page_cache *page)
{
	uint32_t hash = page_cache_hash(page->owner, page->index);

	hash_table_add(&page_cache_hashtable, hash, &page->hash_node);
	list_add_tail(&page->lru_node, &page_cache_lru);
	list_add_tail(&page->mapping_node, &page->owner->pages);
}

struct page_cache *page_cache_get_page(struct page_mapping *mapping,
					    uint64_t index, bool create,
					    bool *created)
{
	struct page_cache *page;

	if (created)
		*created = false;
	if (!mapping)
		return NULL;

	page_cache_init_once();

	page = page_cache_find(mapping, index);
	if (page) {
		page->refcount++;
		list_move_tail(&page->lru_node, &page_cache_lru);
		return page;
	}

	if (!create)
		return NULL;

	/*
	 * Allocation only installs an empty cache page.  Callers that need
	 * valid contents must use page_cache_read_page() or fill and mark the
	 * page uptodate themselves.
	 */
	page = page_cache_alloc_page();
	if (!page)
		return NULL;

	page->owner = mapping;
	page->index = index;
	page->refcount = 1;
	page_cache_add_page(page);
	if (created)
		*created = true;
	return page;
}

struct page_cache *page_cache_read_page(struct page_mapping *mapping,
					     uint64_t index)
{
	struct page_cache *page;
	bool created = false;
	int ret;

	if (!mapping || !mapping->ops || !mapping->ops->readpage)
		return NULL;

	page = page_cache_get_page(mapping, index, true, &created);
	if (!page)
		return NULL;

	if (!page->uptodate) {
		ret = mapping->ops->readpage(mapping, index, page->data);
		if (ret < 0) {
			/*
			 * A newly allocated page must not remain visible after
			 * read failure.  Existing stale pages keep their old
			 * identity but are returned as failure to the caller.
			 */
			if (created)
				page_cache_drop_page(page);
			page_cache_put_page(page);
			return NULL;
		}
		page->uptodate = true;
	}

	return page;
}

struct page_cache *page_cache_get_block(dev_t dev, uint64_t block)
{
	struct page_mapping *mapping = block_device_pages(dev);

	return page_cache_read_page(mapping, block);
}

struct page_cache *page_cache_grab_file_page(struct inode *inode,
						  uint64_t index, bool create,
						  bool *created)
{
	if (!inode)
		return NULL;

	return page_cache_get_page(&inode->i_pages, index, create, created);
}

void page_cache_put_page(struct page_cache *page)
{
	if (!page)
		return;

	if (page->refcount > 0)
		page->refcount--;
	if (page->refcount == 0 && page->dropped)
		page_cache_free_page(page);
}

uint8_t *page_cache_data(struct page_cache *page)
{
	return page ? page->data : NULL;
}

bool page_cache_is_uptodate(const struct page_cache *page)
{
	return page && page->uptodate;
}

void page_cache_set_uptodate(struct page_cache *page, bool uptodate)
{
	if (page)
		page->uptodate = uptodate;
}

void page_cache_truncate_mapping(struct page_mapping *mapping, uint64_t size)
{
	struct list_head *pos;
	struct list_head *next;
	uint64_t tail_index = size / BLOCK_SIZE;
	uint32_t tail_off = (uint32_t)(size % BLOCK_SIZE);

	if (!mapping)
		return;

	/*
	 * Pages beyond EOF cannot be found again through this mapping and must
	 * be dropped.  The partial tail page stays cached, but bytes beyond the
	 * new size are zeroed so later extension cannot expose stale data.
	 */
	list_for_each_safe (pos, next, &mapping->pages) {
		struct page_cache *page =
			list_entry(pos, struct page_cache, mapping_node);

		if (tail_off == 0) {
			if (page->index >= tail_index)
				page_cache_drop_page(page);
			continue;
		}

		if (page->index > tail_index) {
			page_cache_drop_page(page);
			continue;
		}

		if (page->index == tail_index) {
			memset(page->data + tail_off, 0, BLOCK_SIZE - tail_off);
			if (page->uptodate || page->dirty)
				page->uptodate = true;
		}
	}
}

void page_cache_truncate_inode(struct inode *inode, uint64_t size)
{
	if (!inode)
		return;

	page_cache_truncate_mapping(&inode->i_pages, size);
}

void page_cache_invalidate_mapping(struct page_mapping *mapping)
{
	struct list_head *pos;
	struct list_head *next;

	if (!mapping)
		return;

	list_for_each_safe (pos, next, &mapping->pages) {
		struct page_cache *page =
			list_entry(pos, struct page_cache, mapping_node);

		page_cache_drop_page(page);
	}
}

void page_cache_invalidate_inode(struct inode *inode)
{
	if (!inode)
		return;

	page_cache_invalidate_mapping(&inode->i_pages);
}

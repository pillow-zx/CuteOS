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

#include <kernel/page_cache.h>

#include <kernel/blkdev.h>
#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <kernel/fs.h>
#include <kernel/hash.h>
#include <kernel/list.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/timer.h>

#define PAGE_CACHE_HASH_BITS 7
#define PAGE_CACHE_NR_PAGES  512U
#define PAGE_CACHE_WB_ORDER  5U

HASH_TABLE_DECLARE_STATIC(page_cache_hashtable, PAGE_CACHE_HASH_BITS);
static LIST_HEAD(page_cache_lru);
static LIST_HEAD(page_cache_dirty_pages);
static uint32_t page_cache_pages;
static bool page_cache_ready;
static uint8_t *page_cache_wb_buf;
static uint32_t page_cache_wb_pages = 1;

static int page_cache_writeback_run(struct page_cache *start);
static void page_cache_refresh_block_alias(struct page_mapping *mapping,
					   uint32_t blocknr,
					   const void *data);

static void page_cache_sleep_until(uint64_t deadline)
{
	(void)timer_sleep_until(deadline, false);
}

static void page_cache_init_once(void)
{
	if (page_cache_ready)
		return;

	hash_table_init(&page_cache_hashtable);
	INIT_LIST_HEAD(&page_cache_lru);
	INIT_LIST_HEAD(&page_cache_dirty_pages);

	page_cache_wb_buf = get_free_page(PAGE_CACHE_WB_ORDER);
	if (page_cache_wb_buf)
		page_cache_wb_pages = 1u << PAGE_CACHE_WB_ORDER;
	else {
		page_cache_wb_buf = get_free_page(0);
		page_cache_wb_pages = page_cache_wb_buf ? 1u : 0u;
	}

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

static struct page_cache *page_cache_find(struct page_mapping *mapping,
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

static void page_cache_remove_dirty(struct page_cache *page)
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

static void page_cache_free_page(struct page_cache *page)
{
	if (!page)
		return;

	if (!list_empty(&page->hash_node))
		list_del_init(&page->hash_node);
	if (!list_empty(&page->lru_node))
		list_del_init(&page->lru_node);
	if (!list_empty(&page->mapping_node))
		list_del_init(&page->mapping_node);
	if (!list_empty(&page->dirty_node))
		list_del_init(&page->dirty_node);
	if (!list_empty(&page->mapping_dirty_node))
		list_del_init(&page->mapping_dirty_node);

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
	page_cache_remove_dirty(page);
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
	 * unreferenced page first.  writeback_run() may also flush adjacent pages,
	 * after which a clean victim should be available.
	 */
	list_for_each_safe (pos, next, &page_cache_lru) {
		struct page_cache *page =
			list_entry(pos, struct page_cache, lru_node);

		if (page->refcount != 0 || !page->dirty || page->writeback)
			continue;
		if (page_cache_writeback_run(page) < 0)
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
	INIT_LIST_HEAD(&page->mapping_dirty_node);
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

static int page_cache_writeback_run(struct page_cache *start)
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

	while (!list_empty(&mapping->dirty_pages)) {
		struct page_cache *page = list_first_entry(
			&mapping->dirty_pages, struct page_cache,
			mapping_dirty_node);

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

	while (!list_empty(&page_cache_dirty_pages)) {
		struct page_cache *page = list_first_entry(
			&page_cache_dirty_pages, struct page_cache,
			dirty_node);

		ret = page_cache_writeback_run(page);
		if (ret < 0)
			return ret;
	}

	return 0;
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

void page_cache_writeback_thread(void *arg)
{
	(void)arg;

	for (;;) {
		uint64_t deadline = get_mtime() + 5 * MTIME_FREQ;

		page_cache_sleep_until(deadline);
		(void)page_cache_writeback_all();
	}
}

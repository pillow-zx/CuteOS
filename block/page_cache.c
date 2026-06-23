#include <kernel/page_cache.h>

#include <kernel/blkdev.h>
#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <kernel/hash.h>
#include <kernel/list.h>
#include <kernel/sched.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/timer.h>

#define PAGE_CACHE_HASH_BITS 7
#define PAGE_CACHE_NR_PAGES  512U
#define PAGE_CACHE_WB_ORDER  5U

enum page_cache_kind {
	PAGE_CACHE_METADATA = 0,
	PAGE_CACHE_FILE_DATA = 1,
};

struct page_cache_page {
	enum page_cache_kind kind;
	uint8_t *data;
	uint32_t refcount;
	bool uptodate;
	bool dirty;
	bool writeback;
	struct list_head hash_node;
	struct list_head lru_node;
	struct list_head dirty_node;
	struct list_head inode_node;
	struct list_head inode_dirty_node;
	union {
		struct {
			dev_t dev;
			uint64_t blocknr;
		} meta;
		struct {
			struct inode *inode;
			uint64_t index;
		} file;
	} u;
};

HASH_TABLE_DECLARE_STATIC(page_cache_hashtable, PAGE_CACHE_HASH_BITS);
static LIST_HEAD(page_cache_lru);
static LIST_HEAD(page_cache_dirty_pages);
static uint32_t page_cache_pages;
static bool page_cache_ready;
static uint8_t *page_cache_wb_buf;
static uint32_t page_cache_wb_pages = 1;

static void page_cache_sleep_until(uint64_t deadline)
{
	struct timer_wait wait;

	if (deadline <= get_mtime() || !current)
		return;

	timer_wait_init(&wait, current, deadline);
	current->state = TASK_INTERRUPTIBLE;
	timer_wait_start(&wait);

	while (!timer_wait_fired(&wait))
		schedule();

	(void)timer_wait_cancel(&wait);
	if (current->state == TASK_INTERRUPTIBLE ||
	    current->state == TASK_SLEEPING)
		current->state = TASK_RUNNING;
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

static uint32_t page_cache_hash_metadata(dev_t dev, uint64_t blocknr)
{
	return (uint32_t)(dev ^ blocknr ^ (blocknr >> PAGE_CACHE_HASH_BITS));
}

static uint32_t page_cache_hash_file(struct inode *inode, uint64_t index)
{
	uintptr_t key = (uintptr_t)inode;

	return (uint32_t)(key ^ index ^ (index >> PAGE_CACHE_HASH_BITS));
}

static struct page_cache_page *page_cache_find_metadata(dev_t dev,
							uint64_t blocknr)
{
	struct list_head *pos;
	uint32_t hash = page_cache_hash_metadata(dev, blocknr);

	hash_table_for_each_possible (pos, &page_cache_hashtable, hash) {
		struct page_cache_page *page =
			list_entry(pos, struct page_cache_page, hash_node);

		if (page->kind != PAGE_CACHE_METADATA)
			continue;
		if (page->u.meta.dev == dev &&
		    page->u.meta.blocknr == blocknr)
			return page;
	}

	return NULL;
}

static struct page_cache_page *page_cache_find_file(struct inode *inode,
						    uint64_t index)
{
	struct list_head *pos;
	uint32_t hash = page_cache_hash_file(inode, index);

	hash_table_for_each_possible (pos, &page_cache_hashtable, hash) {
		struct page_cache_page *page =
			list_entry(pos, struct page_cache_page, hash_node);

		if (page->kind != PAGE_CACHE_FILE_DATA)
			continue;
		if (page->u.file.inode == inode && page->u.file.index == index)
			return page;
	}

	return NULL;
}

static void page_cache_refresh_metadata_alias(dev_t dev, uint64_t blocknr,
					      const void *data)
{
	struct page_cache_page *page;

	if (!data)
		return;

	page = page_cache_find_metadata(dev, blocknr);
	if (!page)
		return;

	memcpy(page->data, data, BLOCK_SIZE);
	page->uptodate = true;
	page->dirty = false;
}

static void page_cache_remove_dirty(struct page_cache_page *page)
{
	if (!page)
		return;

	if (!list_empty(&page->dirty_node))
		list_del_init(&page->dirty_node);
	if (!list_empty(&page->inode_dirty_node))
		list_del_init(&page->inode_dirty_node);
	page->dirty = false;
}

static void page_cache_free_page(struct page_cache_page *page)
{
	if (!page)
		return;

	if (!list_empty(&page->hash_node))
		hash_table_del(&page->hash_node);
	if (!list_empty(&page->lru_node))
		list_del_init(&page->lru_node);
	if (!list_empty(&page->dirty_node))
		list_del_init(&page->dirty_node);
	if (!list_empty(&page->inode_node))
		list_del_init(&page->inode_node);
	if (!list_empty(&page->inode_dirty_node))
		list_del_init(&page->inode_dirty_node);

	if (page->data)
		free_page(page->data, 0);
	kfree(page);
	if (page_cache_pages > 0)
		page_cache_pages--;
}

static int page_cache_writeback_run(struct page_cache_page *start);

static bool page_cache_evict_one_clean(void)
{
	struct list_head *pos;
	struct list_head *next;

	list_for_each_safe (pos, next, &page_cache_lru) {
		struct page_cache_page *page =
			list_entry(pos, struct page_cache_page, lru_node);

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

	list_for_each_safe (pos, next, &page_cache_lru) {
		struct page_cache_page *page =
			list_entry(pos, struct page_cache_page, lru_node);

		if (page->kind != PAGE_CACHE_FILE_DATA)
			continue;
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

static struct page_cache_page *page_cache_alloc_page(enum page_cache_kind kind)
{
	struct page_cache_page *page;

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

	page->kind = kind;
	INIT_LIST_HEAD(&page->hash_node);
	INIT_LIST_HEAD(&page->lru_node);
	INIT_LIST_HEAD(&page->dirty_node);
	INIT_LIST_HEAD(&page->inode_node);
	INIT_LIST_HEAD(&page->inode_dirty_node);
	page_cache_pages++;

	return page;
}

static void page_cache_add_metadata_page(struct page_cache_page *page)
{
	uint32_t hash = page_cache_hash_metadata(page->u.meta.dev,
						 page->u.meta.blocknr);

	hash_table_add(&page_cache_hashtable, hash, &page->hash_node);
	list_add_tail(&page->lru_node, &page_cache_lru);
}

static void page_cache_add_file_page(struct page_cache_page *page)
{
	uint32_t hash = page_cache_hash_file(page->u.file.inode,
					     page->u.file.index);

	hash_table_add(&page_cache_hashtable, hash, &page->hash_node);
	list_add_tail(&page->lru_node, &page_cache_lru);
	list_add_tail(&page->inode_node, &page->u.file.inode->i_pages);
}

static int page_cache_read_block(dev_t dev, uint64_t blocknr, void *data)
{
	struct block_device *bdev = lookup_block_device(dev);

	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->read_sectors)
		return -ENXIO;

	return bdev->bd_ops->read_sectors(bdev, data,
					  blocknr * BLOCK_SECTORS,
					  BLOCK_SECTORS);
}

static int page_cache_write_block(dev_t dev, uint64_t blocknr, const void *data,
				  uint32_t nsec)
{
	struct block_device *bdev = lookup_block_device(dev);

	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->write_sectors)
		return -ENXIO;

	return bdev->bd_ops->write_sectors(bdev, data,
					   blocknr * BLOCK_SECTORS, nsec);
}

struct page_cache_page *page_cache_get_block(dev_t dev, uint64_t block)
{
	struct page_cache_page *page;

	page_cache_init_once();

	page = page_cache_find_metadata(dev, block);
	if (page) {
		page->refcount++;
		list_move_tail(&page->lru_node, &page_cache_lru);
		return page;
	}

	page = page_cache_alloc_page(PAGE_CACHE_METADATA);
	if (!page)
		return NULL;

	page->u.meta.dev = dev;
	page->u.meta.blocknr = block;
	page->refcount = 1;

	if (page_cache_read_block(dev, block, page->data) < 0) {
		page_cache_free_page(page);
		return NULL;
	}

	page->uptodate = true;
	page_cache_add_metadata_page(page);
	return page;
}

int page_cache_sync_block(struct page_cache_page *page)
{
	int ret;

	if (!page || page->kind != PAGE_CACHE_METADATA)
		return -EINVAL;

	ret = page_cache_write_block(page->u.meta.dev, page->u.meta.blocknr,
				      page->data, BLOCK_SECTORS);
	if (ret == 0) {
		page->dirty = false;
		page->uptodate = true;
	}
	return ret;
}

struct page_cache_page *page_cache_grab_file_page(struct inode *inode,
						  uint64_t index, bool create,
						  bool *created)
{
	struct page_cache_page *page;

	if (created)
		*created = false;
	if (!inode)
		return NULL;

	page_cache_init_once();

	page = page_cache_find_file(inode, index);
	if (page) {
		page->refcount++;
		list_move_tail(&page->lru_node, &page_cache_lru);
		return page;
	}

	if (!create)
		return NULL;

	page = page_cache_alloc_page(PAGE_CACHE_FILE_DATA);
	if (!page)
		return NULL;

	page->u.file.inode = inode;
	page->u.file.index = index;
	page->refcount = 1;
	page_cache_add_file_page(page);
	if (created)
		*created = true;
	return page;
}

void page_cache_put_page(struct page_cache_page *page)
{
	if (!page)
		return;

	if (page->refcount > 0)
		page->refcount--;
}

uint8_t *page_cache_data(struct page_cache_page *page)
{
	return page ? page->data : NULL;
}

bool page_cache_is_uptodate(const struct page_cache_page *page)
{
	return page && page->uptodate;
}

void page_cache_set_uptodate(struct page_cache_page *page, bool uptodate)
{
	if (page)
		page->uptodate = uptodate;
}

bool page_cache_is_dirty(const struct page_cache_page *page)
{
	return page && page->dirty;
}

void page_cache_mark_dirty(struct page_cache_page *page)
{
	if (!page || page->kind != PAGE_CACHE_FILE_DATA)
		return;

	if (!page->dirty) {
		list_add_tail(&page->dirty_node, &page_cache_dirty_pages);
		list_add_tail(&page->inode_dirty_node,
			      &page->u.file.inode->i_dirty_pages);
	}

	page->dirty = true;
	page->uptodate = true;
}

static int page_cache_writeback_run(struct page_cache_page *start)
{
	struct inode *inode;
	struct page_cache_page *pages[32];
	uint32_t pblocks[32];
	uint32_t nr_pages = 0;
	uint32_t run_pages;
	uint64_t start_index;
	uint32_t prev_block;
	int ret;

	if (!start || start->kind != PAGE_CACHE_FILE_DATA || !start->dirty)
		return -EINVAL;
	if (!start->u.file.inode || !start->u.file.inode->i_aops ||
	    !start->u.file.inode->i_aops->map_block ||
	    !start->u.file.inode->i_aops->writepages)
		return -EINVAL;
	if (!page_cache_wb_buf || page_cache_wb_pages == 0)
		return -ENOMEM;

	inode = start->u.file.inode;
	start_index = start->u.file.index;
	run_pages = page_cache_wb_pages;
	if (run_pages > 32u)
		run_pages = 32u;

	prev_block = inode->i_aops->map_block(inode, start_index, false);
	if (!prev_block)
		return -EIO;

	pblocks[0] = prev_block;
	pages[nr_pages++] = start;

	while (nr_pages < run_pages) {
		struct page_cache_page *next;
		uint32_t next_block;
		uint64_t next_index = start_index + nr_pages;

		next = page_cache_find_file(inode, next_index);
		if (!next || !next->dirty || next->writeback)
			break;

		next_block = inode->i_aops->map_block(inode, next_index, false);
		if (!next_block || next_block != prev_block + 1)
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

	ret = inode->i_aops->writepages(inode, start_index, nr_pages,
					 page_cache_wb_buf);
	for (uint32_t i = 0; i < nr_pages; i++) {
		pages[i]->writeback = false;
		if (ret == 0) {
			page_cache_refresh_metadata_alias(inode->i_sb->s_dev,
							  pblocks[i],
							  pages[i]->data);
			page_cache_remove_dirty(pages[i]);
		}
	}

	return ret;
}

int page_cache_writeback_inode(struct inode *inode)
{
	int ret = 0;

	if (!inode)
		return -EINVAL;

	while (!list_empty(&inode->i_dirty_pages)) {
		struct page_cache_page *page = list_first_entry(
			&inode->i_dirty_pages, struct page_cache_page,
			inode_dirty_node);

		ret = page_cache_writeback_run(page);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int page_cache_writeback_all(void)
{
	int ret = 0;

	while (!list_empty(&page_cache_dirty_pages)) {
		struct page_cache_page *page = list_first_entry(
			&page_cache_dirty_pages, struct page_cache_page,
			dirty_node);

		ret = page_cache_writeback_run(page);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void page_cache_drop_file_page(struct page_cache_page *page)
{
	if (!page || page->kind != PAGE_CACHE_FILE_DATA)
		return;

	page_cache_remove_dirty(page);
	page->writeback = false;
	page->uptodate = false;
	memset(page->data, 0, BLOCK_SIZE);

	if (page->refcount == 0)
		page_cache_free_page(page);
}

void page_cache_truncate_inode(struct inode *inode, uint64_t size)
{
	struct list_head *pos;
	struct list_head *next;
	uint64_t tail_index = size / BLOCK_SIZE;
	uint32_t tail_off = (uint32_t)(size % BLOCK_SIZE);

	if (!inode)
		return;

	list_for_each_safe (pos, next, &inode->i_pages) {
		struct page_cache_page *page =
			list_entry(pos, struct page_cache_page, inode_node);

		if (page->kind != PAGE_CACHE_FILE_DATA)
			continue;

		if (tail_off == 0) {
			if (page->u.file.index >= tail_index)
				page_cache_drop_file_page(page);
			continue;
		}

		if (page->u.file.index > tail_index) {
			page_cache_drop_file_page(page);
			continue;
		}

		if (page->u.file.index == tail_index &&
		    (page->uptodate || page->dirty)) {
			memset(page->data + tail_off, 0,
			       BLOCK_SIZE - tail_off);
			page->uptodate = true;
		}
	}
}

void page_cache_invalidate_inode(struct inode *inode)
{
	struct list_head *pos;
	struct list_head *next;

	if (!inode)
		return;

	list_for_each_safe (pos, next, &inode->i_pages) {
		struct page_cache_page *page =
			list_entry(pos, struct page_cache_page, inode_node);

		page_cache_drop_file_page(page);
	}
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

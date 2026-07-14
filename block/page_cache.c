/* Physical page cache keyed by (device, block). */

#include "page_cache_internal.h"

#include <kernel/blkdev.h>
#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <kernel/hash.h>
#include <kernel/list.h>
#include <kernel/slab.h>

constexpr uint32_t PAGE_CACHE_HASH_BITS = 7;
constexpr uint32_t PAGE_CACHE_NR_PAGES = 512U;

HASH_TABLE_DECLARE_STATIC(page_cache_hashtable, PAGE_CACHE_HASH_BITS);
LIST_HEAD_STATIC(page_cache_lru);
LIST_HEAD(page_cache_dirty_list);
LIST_HEAD(page_cache_associations);
DEFINE_SPINLOCK(page_cache_lock);
static uint32_t page_cache_pages;
static bool page_cache_ready;

static uint32_t page_cache_hash(dev_t dev, uint64_t block)
{
	return (uint32_t)(dev ^ block ^ (block >> PAGE_CACHE_HASH_BITS));
}

void page_cache_init_once(void)
{
	if (page_cache_ready)
		return;
	hash_table_init(&page_cache_hashtable);
	INIT_LIST_HEAD(&page_cache_lru);
	INIT_LIST_HEAD(&page_cache_dirty_list);
	INIT_LIST_HEAD(&page_cache_associations);
	page_cache_wb_init();
	page_cache_ready = true;
}

struct page_cache *page_cache_find(dev_t dev, uint64_t block)
{
	struct list_head *pos;
	uint32_t hash = page_cache_hash(dev, block);

	hash_table_for_each_possible (pos, &page_cache_hashtable, hash) {
		struct page_cache *page =
			list_entry(pos, struct page_cache, hash_node);
		if (page->dev == dev && page->block == block)
			return page;
	}
	return NULL;
}

struct page_cache *page_cache_get_data(void *data)
{
	struct list_head *pos;
	struct page_cache *page = NULL;
	irq_flags_t flags;

	if (!data)
		return NULL;
	spin_lock_irqsave(&page_cache_lock, &flags);
	for (uint32_t bucket = 0;
	     bucket < HASH_TABLE_SIZE(page_cache_hashtable.bits) && !page;
	     bucket++) {
		list_for_each (pos, &page_cache_hashtable.buckets[bucket]) {
			struct page_cache *candidate =
				list_entry(pos, struct page_cache, hash_node);

			if (candidate->data != data)
				continue;
			candidate->refcount++;
			list_move_tail(&candidate->lru_node, &page_cache_lru);
			page = candidate;
			break;
		}
	}
	spin_unlock_irqrestore(&page_cache_lock, flags);
	return page;
}

struct page_cache *page_cache_find_mapping(struct page_mapping *mapping,
					   uint64_t index)
{
	struct list_head *pos;
	struct page_cache *page = NULL;
	irq_flags_t flags;
	if (!mapping)
		return NULL;
	spin_lock_irqsave(&page_cache_lock, &flags);
	list_for_each (pos, &page_cache_associations) {
		struct page_cache_assoc *assoc =
			list_entry(pos, struct page_cache_assoc, mapping_node);
		if (assoc->mapping == mapping && assoc->index == index) {
			page = assoc->page;
			page->refcount++;
			list_move_tail(&page->lru_node, &page_cache_lru);
			break;
		}
	}
	spin_unlock_irqrestore(&page_cache_lock, flags);
	return page;
}

static void page_cache_free_page(struct page_cache *page)
{
	if (!page)
		return;
	page_cache_clear_dirty_locked(page);
	page_cache_assoc_remove_page_locked(page);
	if (!list_empty(&page->hash_node))
		list_del_init(&page->hash_node);
	if (!list_empty(&page->lru_node))
		list_del_init(&page->lru_node);
	if (page->data)
		free_page(page->data, 0);
	kfree(page);
	if (page_cache_pages > 0)
		page_cache_pages--;
}

static bool page_cache_evict_one(void)
{
	struct list_head *pos, *next;
	list_for_each_safe (pos, next, &page_cache_lru) {
		struct page_cache *page =
			list_entry(pos, struct page_cache, lru_node);
		if (page->refcount || page->dirty || page->writeback)
			continue;
		page_cache_free_page(page);
		return true;
	}
	return false;
}

static struct page_cache *page_cache_alloc(dev_t dev, uint64_t block)
{
	struct page_cache *page;
	if (page_cache_pages >= PAGE_CACHE_NR_PAGES && !page_cache_evict_one())
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
	page->dev = dev;
	page->block = block;
	INIT_LIST_HEAD(&page->hash_node);
	INIT_LIST_HEAD(&page->lru_node);
	INIT_LIST_HEAD(&page->dirty_node);
	hash_table_add(&page_cache_hashtable, page_cache_hash(dev, block),
		       &page->hash_node);
	list_add_tail(&page->lru_node, &page_cache_lru);
	page_cache_pages++;
	return page;
}

static int page_cache_read_physical(struct page_cache *page)
{
	struct block_device *bdev;
	int ret;
	if (!page)
		return -EINVAL;
	bdev = lookup_block_device(page->dev);
	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->read_sectors)
		return -ENXIO;
	ret = bdev->bd_ops->read_sectors(
		bdev, page->data, page->block * BLOCK_SECTORS, BLOCK_SECTORS);
	if (ret == 0)
		page->uptodate = true;
	return ret;
}

struct page_cache *page_cache_get(dev_t dev, uint64_t block, uint32_t flags,
				  int *error)
{
	struct page_cache *page;
	irq_flags_t irq_flags;
	int ret = 0;
	if (error)
		*error = 0;
	page_cache_init_once();
retry:
	spin_lock_irqsave(&page_cache_lock, &irq_flags);
	page = page_cache_find(dev, block);
	if (!page && !(flags & PAGE_CACHE_CREATE)) {
		spin_unlock_irqrestore(&page_cache_lock, irq_flags);
		if (error)
			*error = -ENODATA;
		return NULL;
	}
	if (!page) {
		page = page_cache_alloc(dev, block);
		if (!page) {
			spin_unlock_irqrestore(&page_cache_lock, irq_flags);
			page = page_cache_dirty_any();
			if (page) {
				ret = page_cache_sync_page(page);
				page_cache_put_page(page);
				if (ret == 0)
					goto retry;
				if (error)
					*error = ret;
				return NULL;
			}
			if (error)
				*error = -ENOMEM;
			return NULL;
		}
	}
	page->refcount++;
	list_move_tail(&page->lru_node, &page_cache_lru);
	spin_unlock_irqrestore(&page_cache_lock, irq_flags);
	if ((flags & PAGE_CACHE_READ) && !page_cache_is_uptodate(page)) {
		ret = page_cache_read_physical(page);
		if (ret < 0) {
			page_cache_put_page(page);
			if (error)
				*error = ret;
			return NULL;
		}
	}
	return page;
}

struct page_cache *page_cache_get_mapping(struct page_mapping *mapping,
					  uint64_t index, uint32_t flags,
					  int *error)
{
	uint64_t block;
	struct page_cache *page;
	struct page_cache *associated;
	int ret;
	if (error)
		*error = 0;
	if (!mapping || !mapping->ops || !mapping->ops->resolve) {
		if (error)
			*error = -EINVAL;
		return NULL;
	}
	associated = page_cache_find_mapping(mapping, index);
	if (associated) {
		page = associated;
		if ((flags & PAGE_CACHE_READ) &&
		    !page_cache_is_uptodate(page)) {
			ret = page_cache_read_physical(page);
			if (ret < 0) {
				page_cache_put_page(page);
				if (error)
					*error = ret;
				return NULL;
			}
		}
		return page;
	}
	ret = mapping->ops->resolve(mapping, index, flags & PAGE_CACHE_CREATE,
				    &block);
	if (ret < 0) {
		if (error)
			*error = ret;
		return NULL;
	}
	page = page_cache_get(
		mapping->dev, block,
		flags | ((flags & PAGE_CACHE_READ) ? PAGE_CACHE_CREATE : 0),
		&ret);
	if (page && page_cache_assoc_add(mapping, index, page) < 0) {
		page_cache_put_page(page);
		page = NULL;
		if (error)
			*error = -ENOMEM;
	}
	if (!page && error)
		*error = ret ? ret : -ENOMEM;
	return page;
}

struct page_cache *page_cache_get_block(dev_t dev, uint64_t block)
{
	return page_cache_get(dev, block, PAGE_CACHE_READ | PAGE_CACHE_CREATE,
			      NULL);
}

void page_cache_put_page(struct page_cache *page)
{
	irq_flags_t flags;
	if (!page)
		return;
	spin_lock_irqsave(&page_cache_lock, &flags);
	BUG_ON(page->refcount == 0);
	page->refcount--;
	if (page->refcount == 0 && page->dropped)
		page_cache_free_page(page);
	spin_unlock_irqrestore(&page_cache_lock, flags);
}

uint8_t *page_cache_data(struct page_cache *page)
{
	return page ? page->data : NULL;
}
bool page_cache_is_uptodate(const struct page_cache *page)
{
	irq_flags_t flags;
	bool uptodate;

	if (!page)
		return false;
	spin_lock_irqsave(&page_cache_lock, &flags);
	uptodate = page->uptodate;
	spin_unlock_irqrestore(&page_cache_lock, flags);
	return uptodate;
}

void page_cache_set_uptodate(struct page_cache *page, bool uptodate)
{
	irq_flags_t flags;

	if (!page)
		return;
	spin_lock_irqsave(&page_cache_lock, &flags);
	page->uptodate = uptodate;
	spin_unlock_irqrestore(&page_cache_lock, flags);
}

bool page_cache_is_dirty(const struct page_cache *page)
{
	irq_flags_t flags;
	bool dirty;

	if (!page)
		return false;
	spin_lock_irqsave(&page_cache_lock, &flags);
	dirty = page->dirty;
	spin_unlock_irqrestore(&page_cache_lock, flags);
	return dirty;
}

void page_cache_truncate_mapping(struct page_mapping *mapping, uint64_t size)
{
	struct list_head *pos, *next;
	uint64_t tail_index;
	uint32_t tail_offset;
	irq_flags_t flags;

	if (!mapping)
		return;
	tail_index = size / BLOCK_SIZE;
	tail_offset = (uint32_t)(size % BLOCK_SIZE);

	spin_lock_irqsave(&page_cache_lock, &flags);
	list_for_each_safe (pos, next, &page_cache_associations) {
		struct page_cache_assoc *assoc =
			list_entry(pos, struct page_cache_assoc, mapping_node);
		struct page_cache *page = assoc->page;

		if (assoc->mapping != mapping)
			continue;
		if (assoc->index < tail_index)
			continue;
		if (assoc->index == tail_index && tail_offset != 0) {
			memset(page->data + tail_offset, 0,
			       BLOCK_SIZE - tail_offset);
			page->uptodate = true;
			if (!page->dirty)
				list_add_tail(&page->dirty_node,
					      &page_cache_dirty_list);
			page->dirty = true;
			continue;
		}
		list_del_init(&assoc->mapping_node);
		kfree(assoc);
		if (!page_cache_assoc_has_page_locked(page)) {
			page_cache_clear_dirty_locked(page);
			page->uptodate = false;
		}
	}
	spin_unlock_irqrestore(&page_cache_lock, flags);
}

void page_cache_invalidate_mapping(struct page_mapping *mapping)
{
	page_cache_assoc_remove_mapping(mapping);
}

void page_cache_truncate_inode(struct inode *inode, uint64_t size)
{
	if (inode)
		page_cache_truncate_mapping(&inode->i_pages, size);
}

void page_cache_invalidate_inode(struct inode *inode)
{
	if (inode)
		page_cache_invalidate_mapping(&inode->i_pages);
}

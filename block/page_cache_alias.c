/* Logical-to-physical association management for the page cache. */

#include <kernel/errno.h>
#include <kernel/slab.h>

#include "page_cache_internal.h"

bool page_cache_assoc_has_page_locked(struct page_cache *page)
{
	struct list_head *pos;

	if (!page)
		return false;
	list_for_each (pos, &page_cache_associations) {
		struct page_cache_assoc *assoc =
			list_entry(pos, struct page_cache_assoc, mapping_node);

		if (assoc->page == page)
			return true;
	}
	return false;
}

int page_cache_assoc_add(struct page_mapping *mapping, uint64_t index,
			 struct page_cache *page)
{
	struct page_cache_assoc *assoc;
	struct list_head *pos;
	irq_flags_t flags;
	int ret = 0;

	if (!mapping || !page)
		return -EINVAL;
	spin_lock_irqsave(&page_cache_lock, &flags);
	list_for_each (pos, &page_cache_associations) {
		assoc = list_entry(pos, struct page_cache_assoc, mapping_node);
		if (assoc->mapping == mapping && assoc->index == index) {
			ret = 0;
			goto unlock;
		}
	}
	assoc = kmalloc(sizeof(*assoc));
	if (!assoc) {
		ret = -ENOMEM;
		goto unlock;
	}
	assoc->mapping = mapping;
	assoc->index = index;
	assoc->page = page;
	INIT_LIST_HEAD(&assoc->page_node);
	INIT_LIST_HEAD(&assoc->mapping_node);
	list_add_tail(&assoc->mapping_node, &page_cache_associations);
unlock:
	spin_unlock_irqrestore(&page_cache_lock, flags);
	return ret;
}

void page_cache_assoc_remove_mapping(struct page_mapping *mapping)
{
	struct list_head *pos, *next;
	irq_flags_t flags;
	if (!mapping)
		return;
	spin_lock_irqsave(&page_cache_lock, &flags);
	list_for_each_safe (pos, next, &page_cache_associations) {
		struct page_cache_assoc *assoc =
			list_entry(pos, struct page_cache_assoc, mapping_node);
		if (assoc->mapping != mapping)
			continue;
		struct page_cache *page = assoc->page;
		list_del_init(&assoc->mapping_node);
		kfree(assoc);
		if (!page_cache_assoc_has_page_locked(page)) {
			page_cache_clear_dirty_locked(page);
			page->uptodate = false;
		}
	}
	spin_unlock_irqrestore(&page_cache_lock, flags);
}

void page_cache_assoc_remove_page_locked(struct page_cache *page)
{
	struct list_head *pos, *next;
	if (!page)
		return;
	list_for_each_safe (pos, next, &page_cache_associations) {
		struct page_cache_assoc *assoc =
			list_entry(pos, struct page_cache_assoc, mapping_node);
		if (assoc->page != page)
			continue;
		list_del_init(&assoc->mapping_node);
		kfree(assoc);
	}
}

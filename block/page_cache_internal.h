#ifndef _CUTEOS_BLOCK_PAGE_CACHE_INTERNAL_H
#define _CUTEOS_BLOCK_PAGE_CACHE_INTERNAL_H

#include <kernel/page_cache.h>

void page_cache_init_once(void);
void page_cache_writeback_init_once(void);

struct page_cache *page_cache_find(struct page_mapping *mapping,
				   uint64_t index);

void page_cache_remove_dirty(struct page_cache *page);
struct page_cache *page_cache_first_dirty_mapping(struct page_mapping *mapping);
struct page_cache *page_cache_first_dirty_global(void);

void page_cache_alias_refresh_after_writeback(struct page_mapping *mapping,
					      uint32_t blocknr,
					      const void *data);
void page_cache_alias_invalidate_after_drop(struct page_cache *page);

int page_cache_writeback_run(struct page_cache *start);

#endif

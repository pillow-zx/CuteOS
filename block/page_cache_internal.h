#ifndef _CUTEOS_BLOCK_PAGE_CACHE_INTERNAL_H
#define _CUTEOS_BLOCK_PAGE_CACHE_INTERNAL_H

#include <kernel/page_cache.h>

void page_cache_init_once(void);
void page_cache_wb_init(void);

struct page_cache *page_cache_find(struct page_mapping *mapping,
				   uint64_t index);

void page_cache_clear_dirty(struct page_cache *page);
struct page_cache *page_cache_dirty_first(struct page_mapping *mapping);
struct page_cache *page_cache_dirty_any(void);

void page_cache_alias_refresh(struct page_mapping *mapping, uint32_t blocknr,
			      const void *data);
void page_cache_alias_invalidate(struct page_cache *page);

int page_cache_wb_run(struct page_cache *start);

#endif

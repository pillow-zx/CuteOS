#ifndef _CUTEOS_KERNEL_PAGE_CACHE_H
#define _CUTEOS_KERNEL_PAGE_CACHE_H

/** @file page_cache.h @brief Physical 4 KiB page-cache interface. */

#include <kernel/compiler.h>
#include <kernel/page_mapping.h>
#include <kernel/types.h>
#include <kernel/vfs.h>

constexpr uint32_t PAGE_CACHE_READ = 1u << 0;
constexpr uint32_t PAGE_CACHE_CREATE = 1u << 1;

struct page_cache;

struct page_cache *__must_check
page_cache_get(dev_t dev, uint64_t block, uint32_t flags, int *error);
struct page_cache *__must_check
page_cache_get_mapping(struct page_mapping *mapping, uint64_t index,
			       uint32_t flags, int *error);
struct page_cache *__must_check page_cache_get_block(dev_t dev,
						     uint64_t block);
struct page_cache *__must_check page_cache_get_data(void *data);
void page_cache_put_page(struct page_cache *page);
uint8_t *__must_check __pure page_cache_data(struct page_cache *page);
bool __must_check page_cache_is_uptodate(const struct page_cache *page);
void page_cache_set_uptodate(struct page_cache *page, bool uptodate);
bool __must_check page_cache_is_dirty(const struct page_cache *page);
void page_cache_mark_dirty(struct page_cache *page);
int __must_check page_cache_sync_page(struct page_cache *page);
int __must_check page_cache_sync_mapping(struct page_mapping *mapping);
int __must_check page_cache_sync_inode(struct inode *inode);
int __must_check page_cache_sync_all(void);
void page_cache_truncate_mapping(struct page_mapping *mapping, uint64_t size);
void page_cache_invalidate_mapping(struct page_mapping *mapping);
void page_cache_truncate_inode(struct inode *inode, uint64_t size);
void page_cache_invalidate_inode(struct inode *inode);
void page_cache_wb_thread(void *arg);

#endif

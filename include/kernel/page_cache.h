#ifndef _CUTEOS_KERNEL_PAGE_CACHE_H
#define _CUTEOS_KERNEL_PAGE_CACHE_H

#include <kernel/blkdev.h>
#include <kernel/fs.h>
#include <kernel/types.h>

struct page_cache_page;

struct page_cache_page *page_cache_get_block(dev_t dev, uint64_t block);
struct page_cache_page *page_cache_grab_file_page(struct inode *inode,
						  uint64_t index, bool create,
						  bool *created);
void page_cache_put_page(struct page_cache_page *page);
uint8_t *page_cache_data(struct page_cache_page *page);
bool page_cache_is_uptodate(const struct page_cache_page *page);
void page_cache_set_uptodate(struct page_cache_page *page, bool uptodate);
bool page_cache_is_dirty(const struct page_cache_page *page);
void page_cache_mark_dirty(struct page_cache_page *page);

int page_cache_writeback_inode(struct inode *inode);
int page_cache_writeback_all(void);
void page_cache_truncate_inode(struct inode *inode, uint64_t size);
void page_cache_invalidate_inode(struct inode *inode);
int page_cache_sync_block(struct page_cache_page *page);

void page_cache_writeback_thread(void *arg);

#endif

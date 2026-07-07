#ifndef _CUTEOS_KERNEL_PAGE_CACHE_H
#define _CUTEOS_KERNEL_PAGE_CACHE_H

/**
 * @file page_cache.h
 * @brief 4 KiB page cache 公共接口。
 */

#include <kernel/page_mapping.h>
#include <kernel/vfs.h>
#include <kernel/types.h>
#include <kernel/compiler.h>

/**
 * @struct page_cache
 * @brief Cached 4 KiB page shared by file and block-device paths.
 *
 * @par Fields
 * - @c owner: Mapping that owns this cached page.
 * - @c index: 4 KiB page index within @ref owner.
 * - @c data: Kernel buffer containing cached bytes.
 * - @c refcount: Pin count held by cache users.
 * - @c uptodate: Data buffer has valid contents from backing store.
 * - @c dirty: Page contains modifications not fully written back.
 * - @c writeback: Page is currently being written to backing storage.
 * - @c dropped: Page was removed from lookup while still referenced.
 * - @c hash_node: Node in global cache hash.
 * - @c lru_node: Node in replacement/LRU list.
 * - @c mapping_node: Node in owner->pages.
 * - @c dirty_node: Node in global dirty list.
 * - @c dirty_map_node: Node in owner->dirty_pages.
 */
struct page_cache {
	struct page_mapping *owner;
	uint64_t index;
	uint8_t *data;
	uint32_t refcount;
	bool uptodate;
	bool dirty;
	bool writeback;
	bool dropped;
	struct list_head hash_node;
	struct list_head lru_node;
	struct list_head mapping_node;
	struct list_head dirty_node;
	struct list_head dirty_map_node;
};

/**
 * @brief Lookup or optionally create a cached page.
 * @param mapping Mapping that owns the page.
 * @param index 4 KiB page index.
 * @param create Whether a missing page may be allocated.
 * @param created Optional output set true when allocation happened.
 * @return Referenced page_cache, or NULL on miss/allocation failure.
 */
struct page_cache *__must_check
page_cache_get_page(struct page_mapping *mapping, uint64_t index, bool create,
		    bool *created);

/**
 * @brief Return an uptodate cached page, reading it from backing storage.
 * @param mapping Mapping that owns the page.
 * @param index 4 KiB page index.
 * @return Referenced page_cache, or NULL/ERR-style failure depending path.
 */
struct page_cache *__must_check
page_cache_read_page(struct page_mapping *mapping, uint64_t index);

/**
 * @brief Lookup or read a raw block-device cache page.
 * @param dev Block device id.
 * @param block 4 KiB block index.
 * @return Referenced page_cache, or NULL on failure.
 */
struct page_cache *__must_check page_cache_get_block(dev_t dev, uint64_t block);

/**
 * @brief Lookup or optionally create a file page owned by an inode.
 * @param inode File inode.
 * @param index 4 KiB file page index.
 * @param create Whether allocation is allowed on a miss.
 * @param created Optional output set true when allocation happened.
 * @return Referenced page_cache, or NULL on failure.
 */
struct page_cache *__must_check page_cache_grab_file_page(struct inode *inode,
							  uint64_t index,
							  bool create,
							  bool *created);

/**
 * @brief Drop one page_cache reference and recycle when possible.
 * @param page Referenced page; may be NULL.
 */
void page_cache_put_page(struct page_cache *page);
uint8_t *__must_check __pure page_cache_data(struct page_cache *page);
bool __must_check __pure page_cache_is_uptodate(const struct page_cache *page);
void page_cache_set_uptodate(struct page_cache *page, bool uptodate);
bool __must_check __pure page_cache_is_dirty(const struct page_cache *page);
/**
 * @brief Mark a cached page dirty and enqueue it for writeback.
 * @param page Page whose contents were modified.
 */
void page_cache_mark_dirty(struct page_cache *page);

/**
 * @brief Write one dirty page back to its mapping.
 * @param page Page to synchronize.
 * @return 0 on success, or a negative errno.
 */
int page_cache_sync_page(struct page_cache *page);

/**
 * @brief Write back all dirty pages owned by a mapping.
 * @param mapping Mapping to synchronize.
 * @return 0 on success, or a negative errno.
 */
int __must_check page_cache_sync_mapping(struct page_mapping *mapping);

/**
 * @brief Write back all dirty pages for an inode.
 * @param inode Inode whose page mapping is synchronized.
 * @return 0 on success, or a negative errno.
 */
int __must_check page_cache_sync_inode(struct inode *inode);

/**
 * @brief Write back all dirty cached pages in the system.
 * @return 0 on success, or a negative errno.
 */
int page_cache_sync_all(void);

/**
 * @brief Drop cached pages beyond a new mapping size.
 * @param mapping Mapping being truncated.
 * @param size New byte size.
 */
void page_cache_truncate_mapping(struct page_mapping *mapping, uint64_t size);

/**
 * @brief Invalidate all cached pages for a mapping.
 * @param mapping Mapping whose cache entries are invalidated.
 */
void page_cache_invalidate_mapping(struct page_mapping *mapping);
void page_cache_truncate_inode(struct inode *inode, uint64_t size);
void page_cache_invalidate_inode(struct inode *inode);

int page_cache_sync_block(struct page_cache *page);

void page_cache_wb_thread(void *arg);

#endif

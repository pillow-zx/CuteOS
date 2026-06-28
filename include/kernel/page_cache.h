#ifndef _CUTEOS_KERNEL_PAGE_CACHE_H
#define _CUTEOS_KERNEL_PAGE_CACHE_H

/*
 * include/kernel/page_cache.h - 4 KiB page cache 公共接口
 *
 * page cache 以 page_mapping 为命名域，缓存单位固定为 BLOCK_SIZE。调用者
 * 负责选择正确的 mapping：
 *   - inode->i_pages 表示文件逻辑块缓存；
 *   - block_device_pages(dev) 表示磁盘物理块缓存。
 *
 * 对文件数据，优先使用 page_cache_read_page()/page_cache_grab_file_page()；
 * 对 ext2 位图、inode 表、super block 等裸磁盘块，继续使用
 * page_cache_get_block()。两者共享同一个缓存实现，但不会混淆逻辑块号和
 * 物理块号。
 */

#include <kernel/page_mapping.h>
#include <kernel/vfs.h>
#include <kernel/types.h>
#include <kernel/compiler.h>

struct page_cache {
	/* Stable cache identity: this page represents owner[index]. */
	struct page_mapping *owner;
	uint64_t index;
	uint8_t *data;
	uint32_t refcount;
	bool uptodate;
	bool dirty;
	bool writeback;

	/*
	 * dropped means the page has been removed from all lookup lists but is
	 * still held by an active caller.  The final put frees it.
	 */
	bool dropped;

	/* Global lookup/LRU membership. */
	struct list_head hash_node;
	struct list_head lru_node;

	/* Per-mapping page and dirty-page membership. */
	struct list_head mapping_node;
	struct list_head dirty_node;
	struct list_head dirty_map_node;
};



/*
 * 返回 (mapping, index) 对应的缓存页。create=false 时仅查找已有页；
 * create=true 时会分配一个未 uptodate 的新页，并通过 @created 告知调用者。
 * 返回页持有一次引用，调用者必须 page_cache_put_page()。
 */
struct page_cache *__must_check
page_cache_get_page(struct page_mapping *mapping, uint64_t index, bool create,
		    bool *created);

/*
 * 查找或创建缓存页，并在页不是 uptodate 时调用 mapping->ops->readpage()
 * 读入数据。失败返回 NULL；成功返回持引用的 uptodate 页。
 */
struct page_cache *__must_check
page_cache_read_page(struct page_mapping *mapping, uint64_t index);

/* 读取块设备物理块缓存页；block 是 4 KiB 块号，不是 512 字节扇区号。 */
struct page_cache *__must_check page_cache_get_block(dev_t dev,
						     uint64_t block);

/* inode 文件页兼容包装；等价于 page_cache_get_page(&inode->i_pages, ...)。 */
struct page_cache *__must_check page_cache_grab_file_page(struct inode *inode,
							  uint64_t index,
							  bool create,
							  bool *created);
void page_cache_put_page(struct page_cache *page);
uint8_t *__must_check __pure page_cache_data(struct page_cache *page);
bool __must_check __pure page_cache_is_uptodate(const struct page_cache *page);
void page_cache_set_uptodate(struct page_cache *page, bool uptodate);
bool __must_check __pure page_cache_is_dirty(const struct page_cache *page);
void page_cache_mark_dirty(struct page_cache *page);

/* 同步单页到其 mapping 后端；成功后会清除脏标记并刷新可能存在的块设备别名。 */
int page_cache_sync_page(struct page_cache *page);

/* 同步一个 mapping、一个 inode 或全局所有脏页。 */
int __must_check page_cache_sync_mapping(struct page_mapping *mapping);
int __must_check page_cache_sync_inode(struct inode *inode);
int page_cache_sync_all(void);

/* 截断/失效某个 mapping 的缓存页；inode 版本是对 i_pages 的包装。 */
void page_cache_truncate_mapping(struct page_mapping *mapping, uint64_t size);
void page_cache_invalidate_mapping(struct page_mapping *mapping);
void page_cache_truncate_inode(struct inode *inode, uint64_t size);
void page_cache_invalidate_inode(struct inode *inode);

/* 块设备页的旧接口名，保留给 ext2 元数据路径使用。 */
int page_cache_sync_block(struct page_cache *page);

void page_cache_wb_thread(void *arg);

#endif

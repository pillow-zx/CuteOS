#ifndef _CUTEOS_KERNEL_PAGE_MAPPING_H
#define _CUTEOS_KERNEL_PAGE_MAPPING_H

/*
 * include/kernel/page_mapping.h - page cache 的后端映射抽象
 *
 * page_mapping 是 page cache 的命名域：同一个 (mapping, index) 唯一标识
 * 一个缓存页。文件 inode 使用自己的 mapping 缓存"文件逻辑块"；块设备使用
 * 自己的 mapping 缓存"磁盘物理块"。page cache 通过 ops 回调完成读、
 * 映射和回写，不感知调用者是 ext2 文件数据、目录数据还是裸块设备元数据。
 *
 * backing 只描述 page cache coherency 关系，不改变 I/O 路由或所有权。
 * 例如 ext2 inode mapping 的 index 是文件逻辑块号，最终落到块设备
 * mapping 的物理块号。inode 页是 authoritative mapping；文件/目录页
 * 回写成功后，page cache 可以用 map_block() 找到物理块，并刷新 backing
 * mapping 中已经存在的 raw block alias。裸块设备 mapping 没有 backing，
 * metadata 写入也不会反向扫描 inode aliases。
 */

#include <kernel/list.h>
#include <kernel/types.h>

struct page_mapping {
	/*
	 * host 指向拥有者：inode mapping 为 struct inode，块设备 mapping 为
	 * struct block_device。page cache 不解引用具体类型，只传回给 ops。
	 */
	void *host;
	const struct page_mapping_ops *ops;

	/*
	 * backing 指向下层 raw block 命名域，仅用于 page cache alias
	 * refresh/invalidate。裸块设备 mapping 没有 backing。
	 */
	struct page_mapping *backing;

	/* 所有驻留缓存页，以及本 mapping 内的脏页子集。 */
	struct list_head pages;
	struct list_head dirty_pages;
};

/*
 * struct page_mapping_ops - mapping 提供给 page cache 的后端操作
 *
 * @readpage:   把 @index 对应的数据读入 @data。对 inode mapping，@index
 *              是文件逻辑块；对块设备 mapping，@index 是物理块号。
 * @map_block:  把 mapping 内部的 @index 翻译为块设备物理块号。create 为
 *              true 时允许后端分配新块；false 时只查询已有映射。
 * @writepages: 从 @start_index 开始写回 @nr_pages 个连续缓存页。调用方只会
 *              在后端确认这些页对应连续物理块后才进行聚合回写。
 *
 * 返回值使用内核负 errno。map_block 通过 @block 返回物理块号，避免用 0
 * 同时表示"有效块 0"和"失败/空洞"。
 */
struct page_mapping_ops {
	int (*readpage)(struct page_mapping *mapping, uint64_t index,
			void *data);
	int (*map_block)(struct page_mapping *mapping, uint64_t index,
			 bool create, uint32_t *block);
	int (*writepages)(struct page_mapping *mapping, uint64_t start_index,
			  uint32_t nr_pages, const void *data);
};

/*
 * page_mapping_init - 初始化一个可被 page cache 使用的命名域
 *
 * 该函数只初始化 mapping 本身，不触发 I/O，也不注册到全局结构。调用者需要
 * 在 host 生命周期内保持 mapping 地址稳定，因为 page cache 用 mapping 指针
 * 作为哈希键的一部分。
 */
static __always_inline void
page_mapping_init(struct page_mapping *mapping, void *host,
		  const struct page_mapping_ops *ops,
		  struct page_mapping *backing)
{
	if (!mapping)
		return;

	mapping->host = host;
	mapping->ops = ops;
	mapping->backing = backing;
	INIT_LIST_HEAD(&mapping->pages);
	INIT_LIST_HEAD(&mapping->dirty_pages);
}

#endif

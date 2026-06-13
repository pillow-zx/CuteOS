/*
 * include/kernel/buffer.h - 块设备 I/O 缓冲区缓存
 *
 * 声明位于文件系统与块设备驱动之间的 buffer cache 层。
 * 每个 buffer_head 描述一个被缓存在内存中的磁盘块。
 *
 * Constants:
 *   BUFFER_HASH_SIZE = 128 (hash table buckets for buffer lookup)
 *
 * Structs:
 *   struct buffer_head - Cached block descriptor (device, block number,
 *                        data pointer, dirty flag, reference count,
 *                        hash linkage, LRU linkage)
 *
 * Functions:
 *   bread(dev, block)  - Read a block from device into cache; returns bh
 *   brelse(bh)         - Release a buffer_head reference
 *   bwrite(bh)         - Write a dirty buffer_head back to disk
 */

#ifndef _CUTEOS_KERNEL_BUFFER_H
#define _CUTEOS_KERNEL_BUFFER_H

#include <kernel/types.h>
#include <kernel/list.h>
#include <kernel/blkdev.h>

#define BLOCK_SIZE	1024u
#define BLOCK_SECTORS	(BLOCK_SIZE / SECTOR_SIZE)

struct buffer_head {
	dev_t b_dev;
	uint64_t b_blocknr;
	uint8_t *b_data;
	uint32_t b_refcnt;
	bool b_dirty;
	struct list_head b_hash;
};

struct buffer_head *bread(dev_t dev, uint64_t block);
void brelse(struct buffer_head *bh);
int bwrite(struct buffer_head *bh);

#endif /* _CUTEOS_KERNEL_BUFFER_H */

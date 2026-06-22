/*
 * block/buffer.c - Buffer Cache（缓冲区缓存）
 *
 * 功能：
 *   实现内核的 Buffer Cache，是块设备 I/O 的核心抽象层。每个 buffer_head
 *   代表一个磁盘块的缓存，包含 dev、blocknr、data、refcount、hash 字段。
 *   使用 128 桶哈希表快速查找。最多缓存 512 个 buffer_head；缓存满时
 *   回收一个未被引用（refcnt==0）的缓冲区，写穿透保证回收不丢数据。
 *
 * 数据结构：
 *   buffer_head {dev, blocknr, data, refcount, hash}
 *   hash_table[128]  - 128 桶哈希表
 *   b_data 通过 kmalloc(1024) 分配
 *
 * 读写策略：
 *   写穿透（write-through）：mark_buffer_dirty 后立即写磁盘。
 *
 * 主要函数：
 *   bread(dev, blocknr)  - 读取指定块。先查哈希表，命中则返回；
 *             未命中则分配 buffer_head 并从磁盘读取后加入哈希表。
 *   brelse(bh)           - 释放 buffer_head 引用（减少 refcount），
 *             缓冲区保留在哈希表中不驱逐。
 *   mark_buffer_dirty(bh) - 标记为脏并立即同步写磁盘（写穿透）。
 */

#include <kernel/buffer.h>
#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <kernel/hash.h>
#include <kernel/list.h>
#include <kernel/slab.h>
#include <kernel/string.h>

#define BUFFER_HASH_BITS 7
#define BUFFER_HASH_SIZE (1u << BUFFER_HASH_BITS)
#define NR_BUFFERS	 512U

HASH_TABLE_DECLARE_STATIC(buffer_hashtable, BUFFER_HASH_BITS);
static uint32_t nr_buffers;
static bool buffer_cache_ready;

static void buffer_cache_init_once(void)
{
	if (buffer_cache_ready)
		return;

	hash_table_init(&buffer_hashtable);
	buffer_cache_ready = true;
}

static uint32_t buffer_hash(dev_t dev, uint64_t block)
{
	return (uint32_t)((dev ^ block) & (BUFFER_HASH_SIZE - 1));
}

static struct buffer_head *find_buffer(dev_t dev, uint64_t block)
{
	struct buffer_head *bh;
	struct list_head *pos;
	uint32_t hash;

	hash = buffer_hash(dev, block);
	hash_table_for_each_possible (pos, &buffer_hashtable, hash) {
		bh = list_entry(pos, struct buffer_head, b_hash);
		if (bh->b_dev == dev && bh->b_blocknr == block)
			return bh;
	}

	return NULL;
}

static void free_buffer(struct buffer_head *bh)
{
	if (!bh)
		return;

	kfree(bh->b_data);
	kfree(bh);
}

/*
 * 缓存满时回收一个未被引用且不脏的缓冲区。写策略为写穿透，brelse 后的
 * 缓冲区 b_dirty 始终为 false，因此回收不会丢失未落盘的数据。命中即移除
 * 并返回 true；找不到可回收项返回 false。
 */
static bool evict_one_buffer(void)
{
	uint32_t i;

	for (i = 0; i < BUFFER_HASH_SIZE; i++) {
		struct list_head *head;
		struct buffer_head *bh;

		head = hash_table_bucket(&buffer_hashtable, i);
		list_for_each_entry (bh, head, b_hash) {
			if (bh->b_refcnt == 0 && !bh->b_dirty) {
				hash_table_del(&bh->b_hash);
				free_buffer(bh);
				nr_buffers--;
				return true;
			}
		}
	}

	return false;
}

static int read_buffer(struct buffer_head *bh)
{
	struct block_device *bdev;
	uint64_t sector;

	bdev = lookup_block_device(bh->b_dev);
	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->read_sectors)
		return -ENXIO;

	sector = bh->b_blocknr * BLOCK_SECTORS;
	return bdev->bd_ops->read_sectors(bdev, bh->b_data, sector,
					  BLOCK_SECTORS);
}

static struct buffer_head *alloc_buffer(dev_t dev, uint64_t block)
{
	struct buffer_head *bh;

	if (nr_buffers >= NR_BUFFERS && !evict_one_buffer())
		return NULL;

	bh = kmalloc(sizeof(*bh));
	if (!bh)
		return NULL;
	memset(bh, 0, sizeof(*bh));

	bh->b_data = kmalloc(BLOCK_SIZE);
	if (!bh->b_data) {
		kfree(bh);
		return NULL;
	}

	bh->b_dev = dev;
	bh->b_blocknr = block;
	bh->b_refcnt = 1;
	bh->b_dirty = false;
	INIT_LIST_HEAD(&bh->b_hash);
	nr_buffers++;

	return bh;
}

struct buffer_head *bread(dev_t dev, uint64_t block)
{
	struct buffer_head *bh;
	uint32_t hash;

	buffer_cache_init_once();

	bh = find_buffer(dev, block);
	if (bh) {
		bh->b_refcnt++;
		return bh;
	}

	bh = alloc_buffer(dev, block);
	if (!bh)
		return NULL;

	if (read_buffer(bh) < 0) {
		free_buffer(bh);
		nr_buffers--;
		return NULL;
	}

	hash = buffer_hash(dev, block);
	hash_table_add(&buffer_hashtable, hash, &bh->b_hash);
	return bh;
}

void brelse(struct buffer_head *bh)
{
	if (!bh)
		return;

	if (bh->b_refcnt > 0)
		bh->b_refcnt--;
}

int bwrite(struct buffer_head *bh)
{
	struct block_device *bdev;
	uint64_t sector;
	int ret;

	if (!bh)
		return -EINVAL;

	bdev = lookup_block_device(bh->b_dev);
	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->write_sectors)
		return -ENXIO;

	sector = bh->b_blocknr * BLOCK_SECTORS;
	ret = bdev->bd_ops->write_sectors(bdev, bh->b_data, sector,
					  BLOCK_SECTORS);
	if (ret == 0)
		bh->b_dirty = false;

	return ret;
}

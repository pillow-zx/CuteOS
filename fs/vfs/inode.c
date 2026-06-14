/*
 * fs/vfs/inode.c - Inode 缓存
 *
 * 功能：
 *   管理 VFS 层 inode 的分配、查找和引用计数。inode 是 VFS 中表示
 *   文件/目录元数据的核心对象。icache 使用 64 桶哈希表，以 (dev, ino)
 *   为键进行索引。iget 先在哈希表中查找，命中则返回，未命中则分配新
 *   inode 并从磁盘读取。iput 减少引用计数，但不移出哈希表（inode
 *   在缓存中持久保留）。struct inode 包含 i_private 指针，供底层
 *   文件系统（如 ext2）挂载私有数据。
 *
 * 主要函数：
 *   iget(sb, ino)  - 在哈希表中查找 inode，未命中则分配并从磁盘读取
 *   iput(inode)    - 减少引用计数，inode 保留在哈希表中不被驱逐
 *   icache_init()  - 初始化 64 桶哈希表
 *
 * 关键数据结构：
 *   struct inode   - 包含 i_private 指针，用于文件系统特定数据
 *   icache 哈希表  - 64 桶，以 (dev, ino) 为键
 */

#include <kernel/errno.h>
#include <kernel/fs.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

#define ICACHE_HASH_BITS 6
#define ICACHE_HASH_SIZE (1u << ICACHE_HASH_BITS)

static struct list_head inode_hashtable[ICACHE_HASH_SIZE];

static uint32_t inode_hash(dev_t dev, uint64_t ino)
{
	return (uint32_t)(dev ^ ino ^ (ino >> ICACHE_HASH_BITS)) &
	       (ICACHE_HASH_SIZE - 1);
}

void icache_init(void)
{
	for (uint32_t i = 0; i < ICACHE_HASH_SIZE; i++)
		INIT_LIST_HEAD(&inode_hashtable[i]);
}

struct inode *inode_alloc(struct super_block *sb, uint64_t ino)
{
	struct inode *inode = kmalloc(sizeof(*inode));
	if (!inode)
		return NULL;

	memset(inode, 0, sizeof(*inode));
	inode->i_ino = ino;
	inode->i_sb = sb;
	inode->i_refcount = 1;
	INIT_LIST_HEAD(&inode->i_hash);
	INIT_LIST_HEAD(&inode->i_sb_list);

	if (sb)
		list_add_tail(&inode->i_sb_list, &sb->s_inodes);

	return inode;
}

static void inode_hash_insert(struct inode *inode)
{
	uint32_t hash = inode_hash(inode->i_sb->s_dev, inode->i_ino);

	list_add(&inode->i_hash, &inode_hashtable[hash]);
}

struct inode *iget(struct super_block *sb, uint64_t ino)
{
	if (!sb)
		return NULL;

	uint32_t hash = inode_hash(sb->s_dev, ino);
	struct list_head *pos;

	list_for_each(pos, &inode_hashtable[hash]) {
		struct inode *inode = list_entry(pos, struct inode, i_hash);

		if (inode->i_sb == sb && inode->i_ino == ino) {
			igrab(inode);
			return inode;
		}
	}

	struct inode *inode = inode_alloc(sb, ino);
	if (!inode)
		return NULL;

	if (sb->s_op && sb->s_op->read_inode) {
		int ret = sb->s_op->read_inode(inode);
		if (ret < 0) {
			list_del(&inode->i_sb_list);
			kfree(inode);
			return NULL;
		}
	}

	inode_hash_insert(inode);
	return inode;
}

void igrab(struct inode *inode)
{
	if (inode)
		inode->i_refcount++;
}

void iput(struct inode *inode)
{
	if (!inode)
		return;

	if (inode->i_refcount == 0)
		return;

	inode->i_refcount--;
}

void inode_forget(struct inode *inode)
{
	if (!inode)
		return;

	if (inode->i_hash.next && inode->i_hash.prev)
		list_del(&inode->i_hash);
	if (inode->i_sb_list.next && inode->i_sb_list.prev)
		list_del(&inode->i_sb_list);

	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->evict_inode)
		inode->i_sb->s_op->evict_inode(inode);

	kfree(inode);
}

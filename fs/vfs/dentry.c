/*
 * fs/vfs/dentry.c - Dentry 缓存
 *
 * 功能：
 *   管理 VFS 层的目录项缓存（dcache）。dentry 是路径分量与 inode
 *   之间的桥梁，将文件名与对应的 inode 关联起来。dcache 使用 128 桶
 *   哈希表，以 (parent, name) 为键进行索引。本实现不做驱逐，dentry
 *   创建后永久留在哈希表中。dget/dput 管理引用计数。d_parent 指针
 *   用于 ".." 路径遍历时回溯到父目录。
 *
 * 主要函数：
 *   dget(dentry)             - 增加 dentry 引用计数
 *   dput(dentry)             - 减少引用计数（dentry 仍在哈希表中）
 *   dcache_lookup(parent, name) - 在 128 桶哈希表中查找 dentry
 *   dcache_insert(dentry)    - 将 dentry 插入哈希表
 *   dcache_init()            - 初始化 128 桶哈希表
 *
 * 关键数据结构：
 *   dentry.d_parent          - 指向父 dentry，用于 ".." 遍历
 *   dcache 哈希表            - 128 桶，以 (parent, name) 为键，无驱逐策略
 */

#include <kernel/errno.h>
#include <kernel/fs.h>
#include <kernel/hash.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

#define DCACHE_HASH_BITS 7
#define DCACHE_HASH_SIZE (1u << DCACHE_HASH_BITS)

HASH_TABLE_DECLARE_STATIC(dentry_hashtable, DCACHE_HASH_BITS);

static uint32_t dentry_hash(struct dentry *parent, const char *name,
			    size_t namelen)
{
	uintptr_t key = (uintptr_t)parent;

	for (size_t i = 0; i < namelen; i++)
		key = (key * 33u) ^ (uint8_t)name[i];

	return (uint32_t)key & (DCACHE_HASH_SIZE - 1);
}

static bool dentry_hashed(struct dentry *dentry)
{
	return dentry && dentry->d_hash.next && dentry->d_hash.prev &&
	       !list_empty(&dentry->d_hash);
}

void dcache_init(void)
{
	hash_table_init(&dentry_hashtable);
}

struct dentry *dentry_alloc(struct dentry *parent, const char *name,
			    size_t namelen)
{
	if (!name || namelen > VFS_NAME_MAX)
		return NULL;

	struct dentry *dentry = kmalloc(sizeof(*dentry));
	if (!dentry)
		return NULL;

	memset(dentry, 0, sizeof(*dentry));
	memcpy(dentry->d_name, name, namelen);
	dentry->d_name[namelen] = '\0';
	dentry->d_namelen = (uint8_t)namelen;
	refcount_set(&dentry->d_refcount, 1);
	dentry->d_parent = parent ? parent : dentry;
	dentry->d_sb = parent ? parent->d_sb : NULL;
	INIT_LIST_HEAD(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_child);
	INIT_LIST_HEAD(&dentry->d_subdirs);

	if (parent)
		list_add_tail(&dentry->d_child, &parent->d_subdirs);

	return dentry;
}

struct dentry *dcache_lookup(struct dentry *parent, const char *name,
			     size_t namelen)
{
	if (!parent || !name || namelen > VFS_NAME_MAX)
		return NULL;

	uint32_t hash = dentry_hash(parent, name, namelen);
	struct list_head *pos;

	hash_table_for_each_possible (pos, &dentry_hashtable, hash) {
		struct dentry *dentry = list_entry(pos, struct dentry, d_hash);

		if (dentry->d_parent != parent || dentry->d_namelen != namelen)
			continue;
		if (memcmp(dentry->d_name, name, namelen) != 0)
			continue;

		dget(dentry);
		return dentry;
	}

	return NULL;
}

void dcache_insert(struct dentry *dentry)
{
	if (!dentry || !dentry->d_parent)
		return;
	if (dentry_hashed(dentry))
		return;

	uint32_t hash = dentry_hash(dentry->d_parent, dentry->d_name,
				    dentry->d_namelen);

	hash_table_add(&dentry_hashtable, hash, &dentry->d_hash);
}

void dcache_invalidate(struct dentry *dentry)
{
	if (!dentry)
		return;
	if (dentry_hashed(dentry))
		hash_table_del(&dentry->d_hash);
	else
		INIT_LIST_HEAD(&dentry->d_hash);

	dentry->d_inode = NULL;
}

void dcache_move(struct dentry *dentry, struct dentry *new_parent,
		 const char *new_name, size_t new_namelen)
{
	if (!dentry || !new_parent || !new_name || new_namelen > VFS_NAME_MAX)
		return;

	if (dentry_hashed(dentry))
		hash_table_del(&dentry->d_hash);
	else
		INIT_LIST_HEAD(&dentry->d_hash);

	if (dentry->d_parent != new_parent)
		list_move_tail(&dentry->d_child, &new_parent->d_subdirs);

	memcpy(dentry->d_name, new_name, new_namelen);
	dentry->d_name[new_namelen] = '\0';
	dentry->d_namelen = (uint8_t)new_namelen;
	dentry->d_parent = new_parent;
	dentry->d_sb = new_parent->d_sb;
	dcache_insert(dentry);
}

void dget(struct dentry *dentry)
{
	if (dentry)
		refcount_inc_allow_zero(&dentry->d_refcount);
}

void dput(struct dentry *dentry)
{
	if (!dentry)
		return;

	(void)refcount_dec_if_positive(&dentry->d_refcount);
}

int vfs_stat_dentry(struct dentry *dentry, struct stat *st)
{
	if (!dentry || !st)
		return -EINVAL;

	return vfs_stat_inode(vfs_dentry_inode(dentry), st);
}

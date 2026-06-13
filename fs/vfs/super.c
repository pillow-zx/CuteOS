/*
 * fs/vfs/super.c - VFS 超级块管理
 *
 * 功能：
 *   实现 VFS 层的超级块管理。超级块（super_block）是每个已挂载文件系统
 *   在内核中的核心描述结构。本模块负责文件系统类型的注册和超级块的创建。
 *   register_filesystem() 将文件系统类型注册到全局数组 fs_types[8] 中。
 *   ext2_mount 从磁盘读取超级块，创建对应的 VFS super_block 结构。
 *   super_operations 操作向量包含 read_inode、write_inode、evict_inode，
 *   由底层具体文件系统实现。
 *
 * 主要函数：
 *   register_filesystem(fs_type) - 将文件系统类型注册到 fs_types[8] 数组
 *   ext2_mount(dev, dir)         - 读取磁盘超级块，创建 VFS super_block
 *   super_operations             - {read_inode, write_inode, evict_inode} 操作向量
 */

#include <kernel/errno.h>
#include <kernel/fs.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

#define NR_FILESYSTEMS 8

static struct file_system_type *fs_types[NR_FILESYSTEMS];

int register_filesystem(struct file_system_type *fs_type)
{
	if (!fs_type || !fs_type->name)
		return -EINVAL;

	for (uint32_t i = 0; i < NR_FILESYSTEMS; i++) {
		if (fs_types[i] && strcmp(fs_types[i]->name, fs_type->name) == 0)
			return -EINVAL;
	}

	for (uint32_t i = 0; i < NR_FILESYSTEMS; i++) {
		if (!fs_types[i]) {
			fs_types[i] = fs_type;
			fs_type->next = NULL;
			return 0;
		}
	}

	return -ENFILE;
}

struct file_system_type *get_filesystem_type(const char *name)
{
	if (!name)
		return NULL;

	for (uint32_t i = 0; i < NR_FILESYSTEMS; i++) {
		if (fs_types[i] && strcmp(fs_types[i]->name, name) == 0)
			return fs_types[i];
	}

	return NULL;
}

struct super_block *super_alloc(struct file_system_type *fs_type, dev_t dev)
{
	struct super_block *sb = kmalloc(sizeof(*sb));
	if (!sb)
		return NULL;

	memset(sb, 0, sizeof(*sb));
	sb->s_dev = dev;
	sb->s_type = fs_type;
	INIT_LIST_HEAD(&sb->s_inodes);

	return sb;
}

void vfs_init(void)
{
	icache_init();
	dcache_init();

	for (uint32_t i = 0; i < NR_FILESYSTEMS; i++)
		fs_types[i] = NULL;
}

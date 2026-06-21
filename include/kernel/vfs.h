#ifndef _CUTEOS_KERNEL_VFS_H
#define _CUTEOS_KERNEL_VFS_H

/*
 * include/kernel/vfs.h - VFS 内部声明
 *
 * VFS 内部辅助函数声明。大部分 VFS 类型和操作定义在 kernel/fs.h 中；
 * 本头文件补充内部路由和查找辅助函数。未来可能会合并到 fs.h 中。
 */

#include <kernel/fs.h>

struct inode *inode_alloc(struct super_block *sb, uint64_t ino);
struct inode *iget(struct super_block *sb, uint64_t ino);
void igrab(struct inode *inode);
void iput(struct inode *inode);
void inode_forget(struct inode *inode);
void icache_init(void);

struct dentry *dentry_alloc(struct dentry *parent, const char *name,
			    size_t namelen);
struct dentry *dcache_lookup(struct dentry *parent, const char *name,
			     size_t namelen);
void dcache_insert(struct dentry *dentry);
void dget(struct dentry *dentry);
void dput(struct dentry *dentry);
void dcache_init(void);

extern struct dentry *root_dentry;

/* path_lookup() flags */
#define LOOKUP_NOFOLLOW 0x0001 /* 不跟随路径末端的符号链接 */

#define VFS_MAY_EXEC  0x1
#define VFS_MAY_WRITE 0x2
#define VFS_MAY_READ  0x4

struct dentry *path_lookup(const char *path, uint32_t flags);
int path_lookup_err(const char *path, uint32_t flags, struct dentry **res);
int path_lookupat_err(struct dentry *base, const char *path, uint32_t flags,
		      struct dentry **res);
struct dentry *path_parent_lookup(const char *path, char *name,
				  size_t *namelen);
int path_parent_lookupat_err(struct dentry *base, const char *path, char *name,
			     size_t *namelen, struct dentry **res);
int vfs_inode_permission(struct inode *inode, uint32_t mask);
int vfs_readlink(struct dentry *dentry, char *buf, size_t size);
int vfs_create(const char *path, uint32_t mode, struct dentry **res);
int vfs_create_at(struct dentry *base, const char *path, uint32_t mode,
		  struct dentry **res);
int vfs_mkdir(const char *path, uint32_t mode);
int vfs_mkdir_at(struct dentry *base, const char *path, uint32_t mode);
int vfs_unlink(const char *path, int flags);
int vfs_unlink_at(struct dentry *base, const char *path, int flags);
int vfs_mknod(const char *path, uint32_t mode, dev_t dev);
int vfs_mknod_at(struct dentry *base, const char *path, uint32_t mode,
		 dev_t dev);
int vfs_stat_dentry(struct dentry *dentry, struct kstat *st);
int vfs_chdir_dentry(struct dentry *dentry);
void vfs_set_root_dentry(struct dentry *dentry);

int register_filesystem(struct file_system_type *fs_type);
struct file_system_type *get_filesystem_type(const char *name);
struct super_block *super_alloc(struct file_system_type *fs_type, dev_t dev);
void vfs_init(void);
int mount_root(void);

#endif

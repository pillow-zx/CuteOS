#ifndef _CUTEOS_KERNEL_VFS_H
#define _CUTEOS_KERNEL_VFS_H

/*
 * include/kernel/vfs.h - VFS 内部声明
 */

#include <kernel/fs.h>

struct inode *__must_check inode_alloc(struct super_block *sb, uint64_t ino);
struct inode *__must_check iget(struct super_block *sb, uint64_t ino);
void igrab(struct inode *inode);
void iput(struct inode *inode);
void inode_forget(struct inode *inode);
void icache_init(void);

struct dentry *__must_check dentry_alloc(struct dentry *parent,
					 const char *name, size_t namelen);
struct dentry *__must_check dcache_lookup(struct dentry *parent,
					  const char *name, size_t namelen);
void dcache_insert(struct dentry *dentry);
void dcache_invalidate(struct dentry *dentry);
void dcache_move(struct dentry *dentry, struct dentry *new_parent,
		 const char *new_name, size_t new_namelen);
void dget(struct dentry *dentry);
void dput(struct dentry *dentry);
void dcache_init(void);

extern struct dentry *root_dentry;

#define LOOKUP_NOFOLLOW 0x0001
#define LOOKUP_NO_MOUNT 0x0002

#define VFS_MAY_EXEC  0x1
#define VFS_MAY_WRITE 0x2
#define VFS_MAY_READ  0x4

int __must_check path_lookupat_path(const struct path *base, const char *path,
				    uint32_t flags, struct path *res);
int __must_check path_parent_lookupat_path(const struct path *base,
					   const char *path, char *name,
					   size_t *namelen, struct path *res);
int __must_check vfs_init_inode_owner(struct inode *inode);
int __must_check vfs_inode_permission(struct inode *inode, uint32_t mask);
int __must_check vfs_readlink(struct dentry *dentry, char *buf, size_t size);
int __must_check vfs_create_at_path(const struct path *base, const char *path,
				    uint32_t mode, struct path *res);
int __must_check vfs_symlink_at_path(const struct path *base,
				     const char *target, const char *linkpath);
int __must_check vfs_link_at_path(struct dentry *old_dentry,
				  const struct path *new_base,
				  const char *new_path);
int __must_check vfs_mkdir_at_path(const struct path *base, const char *path,
				   uint32_t mode);
int vfs_unlink_at_path(const struct path *base, const char *path, int flags);
int __must_check vfs_rename_at_path(const struct path *old_base,
				    const char *old_path,
				    const struct path *new_base,
				    const char *new_path, unsigned int flags);
int __must_check vfs_mknod_at_path(const struct path *base, const char *path,
				   uint32_t mode, dev_t dev);
int __must_check vfs_stat_dentry(struct dentry *dentry, struct stat *st);
int __must_check vfs_chdir_path(const struct path *path);
void mntget(struct vfsmount *mnt);
void mntput(struct vfsmount *mnt);
void path_get(const struct path *path);
void path_put(struct path *path);
int __must_check vfs_root_path(struct path *path);
int __must_check vfs_path_from_dentry(struct dentry *dentry, struct path *path);
int __must_check vfs_mount_root(dev_t dev);
int __must_check vfs_mount(const char *source, const char *target,
			   const char *type, unsigned long flags,
			   const void *data);
int __must_check vfs_umount(const char *target, int flags);
int __must_check vfs_follow_mount(struct path *path);
int __must_check vfs_follow_dotdot_mount(struct path *path);
int __must_check vfs_register_chrdev(dev_t dev,
				     const struct file_operations *fops);
const struct file_operations *__must_check vfs_chrdev_fops(dev_t dev);

int __must_check register_filesystem(struct file_system_type *fs_type);
struct file_system_type *__must_check get_filesystem_type(const char *name);
struct file_system_type *__must_check
get_next_filesystem_type(struct file_system_type *prev);
struct super_block *__must_check super_alloc(struct file_system_type *fs_type,
					     dev_t dev);
void vfs_init(void);
int __must_check filesystems_init(void);

#ifdef KERNEL_SELFTEST
int __must_check unregister_filesystem(struct file_system_type *fs_type);
int __must_check vfs_test_select_rootfs(dev_t dev,
					struct file_system_type **out_fs);
#endif

#endif

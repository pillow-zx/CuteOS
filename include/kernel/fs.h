#ifndef _CUTEOS_KERNEL_FS_H
#define _CUTEOS_KERNEL_FS_H

/*
 * include/kernel/fs.h - 虚拟文件系统 (VFS) 核心对象与操作
 *
 * 声明四个基本 VFS 对象及其操作向量，每个具体文件系统都必须实现。
 * VFS 层将通用文件操作翻译为具体文件系统的动作。
 *
 * Core objects:
 *   struct super_block      - Mounted filesystem instance (device, block size,
 *                             root dentry, super_operations pointer)
 *   struct inode            - File metadata (ino, mode, size, nlink, ops,
 *                             page-cache page_mapping)
 *   struct dentry           - dentry cache (name, parent inode,
 *                             child linkage, reference count)
 *   struct file             - Open file descriptor (dentry, position, flags,
 *                             file_operations pointer)
 *
 * Operation vectors:
 *   struct super_operations  - read_inode, write_inode, sync_fs
 *   struct inode_operations - lookup, create, link, unlink, mkdir
 *   struct file_operations  - read, write, llseek, open, release
 *
 * Callback type:
 *   filldir_t - Callback for readdir to fill directory entries
 *
 * Functions:
 *   register_filesystem(fs) - Register a filesystem driver
 *   mount_root()            - Mount the root filesystem
 */

#include <kernel/page_mapping.h>
#include <kernel/list.h>
#include <kernel/refcount.h>
#include <kernel/types.h>
#include <kernel/compiler.h>
#include <kernel/wait.h>
#include <uapi/dirent.h>
#include <uapi/fcntl.h>
#include <uapi/poll.h>
#include <uapi/stat.h>

struct task_struct;
struct trap_frame;
struct file;
struct inode;
struct dentry;
struct super_block;
struct statfs64;
struct vfs_poll_table;

#define VFS_NAME_MAX 255
#define VFS_PATH_MAX 4096

#define KERN_STDIN  0
#define KERN_STDOUT 1
#define KERN_STDERR 2

#define NR_OPEN 32

#define FMODE_READ  0x1
#define FMODE_WRITE 0x2

typedef int (*filldir_t)(void *ctx, const char *name, size_t namelen,
			 uint64_t ino, uint8_t type, loff_t off);

struct super_operations {
	int (*read_inode)(struct inode *inode);
	int (*write_inode)(struct inode *inode);
	void (*evict_inode)(struct inode *inode);
	int (*sync_fs)(struct super_block *sb);
	int (*statfs)(struct super_block *sb, struct statfs64 *buf);
};

struct inode_operations {
	struct dentry *(*lookup)(struct inode *dir, struct dentry *dentry);
	int (*create)(struct inode *dir, struct dentry *dentry, uint32_t mode);
	int (*symlink)(struct inode *dir, struct dentry *dentry,
		       const char *target);
	int (*link)(struct dentry *old_dentry, struct inode *dir,
		    struct dentry *new_dentry);
	int (*unlink)(struct inode *dir, struct dentry *dentry);
	int (*mkdir)(struct inode *dir, struct dentry *dentry, uint32_t mode);
	int (*rmdir)(struct inode *dir, struct dentry *dentry);
	int (*readlink)(struct inode *inode, char *buf, size_t size);
	int (*truncate)(struct inode *inode, uint64_t size);
	int (*rename)(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry,
		      unsigned int flags);
};

struct file_operations {
	ssize_t (*read)(struct file *file, char *buf, size_t count);
	ssize_t (*write)(struct file *file, const char *buf, size_t count);
	loff_t (*llseek)(struct file *file, loff_t offset, int whence);
	int (*open)(struct inode *inode, struct file *file);
	int (*readdir)(struct file *file, void *ctx, filldir_t filldir);
	uint32_t (*poll)(struct file *file, uint32_t events,
			 struct vfs_poll_table *table);
	int (*ioctl)(struct file *file, uint64_t cmd, uint64_t arg);
	int (*release)(struct file *file);
};

struct file_system_type {
	const char *name;
	struct super_block *(*mount)(struct file_system_type *fs_type,
				     dev_t dev, void *data);
	struct file_system_type *next;
};

struct super_block {
	dev_t s_dev;
	uint32_t s_blocksize;
	uint32_t s_flags;
	struct dentry *s_root;
	const struct super_operations *s_op;
	struct file_system_type *s_type;
	void *s_private;
	struct list_head s_inodes;
};

struct inode {
	uint64_t i_ino;
	uint32_t i_mode;
	uint32_t i_uid;
	uint32_t i_gid;
	uint32_t i_nlink;
	uint64_t i_size;
	int64_t i_atime_sec;
	int64_t i_mtime_sec;
	int64_t i_ctime_sec;
	dev_t i_rdev;
	refcount_t i_refcount;
	struct super_block *i_sb;
	const struct inode_operations *i_op;
	const struct file_operations *i_fop;

	/*
	 * i_pages owns cached file logical blocks for regular files,
	 * directories, and symlinks that store data in blocks.  Filesystems set
	 * ops/backing according to inode type; VFS/page cache code uses this
	 * field generically and must not know ext2 block-layout details.
	 */
	struct page_mapping i_pages;
	void *i_private;
	struct list_head i_hash;
	struct list_head i_sb_list;
};

struct dentry {
	char d_name[VFS_NAME_MAX + 1];
	uint8_t d_namelen;
	refcount_t d_refcount;
	struct inode *d_inode;
	struct dentry *d_parent;
	struct super_block *d_sb;
	void *d_fsdata;
	struct list_head d_hash;
	struct list_head d_child;
	struct list_head d_subdirs;
};

struct vfsmount {
	refcount_t mnt_refcount;
	atomic_t mnt_active_refs;
	struct list_head mnt_list;
	struct vfsmount *mnt_parent;
	struct dentry *mnt_mountpoint;
	struct dentry *mnt_root;
	struct super_block *mnt_sb;
	dev_t mnt_dev;
	bool mnt_is_root;
};

struct path {
	struct vfsmount *mnt;
	struct dentry *dentry;
};

struct file {
	const struct file_operations *f_op;
	struct path f_path;
	struct inode *f_inode;
	void *private_data;
	loff_t f_pos;
	uint32_t f_flags;
	uint32_t f_mode;
	refcount_t refcount;
	bool static_file;
};

#define VFS_POLL_MAX_WAIT_QUEUES (NR_OPEN * 2)

struct vfs_poll_entry {
	struct wait_queue_head *wq;
	struct wait_queue_entry wait;
};

struct vfs_poll_table {
	struct vfs_poll_entry entries[VFS_POLL_MAX_WAIT_QUEUES];
	size_t nr_entries;
};

typedef int (*vfs_poll_scan_t)(struct vfs_poll_table *table, void *arg);

int __must_check vfs_open(const char *path, uint32_t flags, uint32_t mode);
int __must_check vfs_openat_path(const struct path *base, const char *path,
				 uint32_t flags, uint32_t mode);
int __must_check vfs_openat(struct dentry *base, const char *path,
			    uint32_t flags, uint32_t mode);
int __must_check file_get_status_flags(struct file *file);
int __must_check file_set_status_flags(struct file *file, uint32_t flags);
ssize_t __must_check vfs_read(struct file *file, char *buf, size_t count);
ssize_t __must_check vfs_write(struct file *file, const char *buf,
			       size_t count);
loff_t __must_check vfs_llseek(struct file *file, loff_t offset, int whence);
int __must_check vfs_readdir(struct file *file, void *ctx, filldir_t filldir);
int __must_check vfs_truncate_file(struct file *file, uint64_t size);
int __must_check vfs_inode_truncate(struct inode *inode, uint64_t size);
int __must_check vfs_inode_writeback(struct inode *inode);
int __must_check vfs_inode_set_timestamps(struct inode *inode,
					  int64_t atime_sec, int64_t mtime_sec,
					  bool set_atime, bool set_mtime);
int __must_check vfs_inode_touch(struct inode *inode, bool atime, bool mtime,
				 bool ctime);
int __must_check vfs_sync_file(struct file *file);
int __must_check vfs_stat_inode(const struct inode *inode, struct stat *st);
int __must_check vfs_stat_file(struct file *file, struct stat *st);
int __must_check vfs_statfs(struct super_block *sb, struct statfs64 *buf);
void vfs_poll_table_init(struct vfs_poll_table *table);
void vfs_poll_table_cleanup(struct vfs_poll_table *table);
void vfs_poll_wait(struct vfs_poll_table *table, struct wait_queue_head *wq);
int __must_check vfs_poll_wait_until(vfs_poll_scan_t scan, void *arg,
				     bool has_timeout, uint64_t deadline);
uint32_t __must_check vfs_poll(struct file *file, uint32_t events,
			       struct vfs_poll_table *table);
int __must_check vfs_ioctl(struct file *file, uint64_t cmd, uint64_t arg);
int __must_check vfs_getcwd_path(const struct path *cwd, char *buf,
				 size_t size);

static __always_inline __must_check __pure uint64_t
vfs_inode_size(const struct inode *inode)
{
	return inode ? inode->i_size : 0;
}

static __always_inline __must_check __pure uint64_t
vfs_inode_number(const struct inode *inode)
{
	return inode ? inode->i_ino : 0;
}

static __always_inline __must_check __pure uint32_t
vfs_inode_mode(const struct inode *inode)
{
	return inode ? inode->i_mode : 0;
}

static __always_inline __must_check __pure dev_t
vfs_inode_rdev(const struct inode *inode)
{
	return inode ? inode->i_rdev : 0;
}

static __always_inline __must_check __pure uint32_t
vfs_inode_uid(const struct inode *inode)
{
	return inode ? inode->i_uid : 0;
}

static __always_inline __must_check __pure uint32_t
vfs_inode_gid(const struct inode *inode)
{
	return inode ? inode->i_gid : 0;
}

static __always_inline __must_check __pure uint32_t
vfs_inode_nlink(const struct inode *inode)
{
	return inode ? inode->i_nlink : 0;
}

static __always_inline __must_check __pure int64_t
vfs_inode_atime_sec(const struct inode *inode)
{
	return inode ? inode->i_atime_sec : 0;
}

static __always_inline __must_check __pure int64_t
vfs_inode_mtime_sec(const struct inode *inode)
{
	return inode ? inode->i_mtime_sec : 0;
}

static __always_inline __must_check __pure int64_t
vfs_inode_ctime_sec(const struct inode *inode)
{
	return inode ? inode->i_ctime_sec : 0;
}

static __always_inline __must_check __pure dev_t
vfs_inode_dev(const struct inode *inode)
{
	return inode && inode->i_sb ? inode->i_sb->s_dev : 0;
}

static __always_inline __must_check __pure struct inode *
vfs_dentry_inode(struct dentry *dentry)
{
	return dentry ? dentry->d_inode : NULL;
}

static __always_inline __must_check __pure struct dentry *
vfs_dentry_parent(struct dentry *dentry)
{
	return dentry ? dentry->d_parent : NULL;
}

static __always_inline __must_check __pure const char *
vfs_dentry_name(struct dentry *dentry)
{
	return dentry ? dentry->d_name : NULL;
}

static __always_inline __must_check __pure size_t
vfs_dentry_namelen(struct dentry *dentry)
{
	return dentry ? dentry->d_namelen : 0;
}

#endif

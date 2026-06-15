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
 *   struct inode            - File metadata (ino, mode, size, nlink, ops)
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

#include <kernel/list.h>
#include <kernel/types.h>

struct task_struct;
struct trap_frame;
struct file;
struct inode;
struct dentry;
struct super_block;

#define VFS_NAME_MAX 255
#define VFS_PATH_MAX 4096

#define KERN_STDIN  0
#define KERN_STDOUT 1
#define KERN_STDERR 2

#define NR_OPEN 32

#define FMODE_READ  0x1
#define FMODE_WRITE 0x2

#define O_RDONLY  00000000
#define O_WRONLY  00000001
#define O_RDWR	  00000002
#define O_ACCMODE 00000003
#define O_CREAT	  00000100
#define O_EXCL	  00000200
#define O_TRUNC	  00001000
#define O_APPEND  00002000
#define O_DIRECTORY 00200000

#define AT_FDCWD	-100
#define AT_REMOVEDIR	0x200

#define DT_UNKNOWN 0
#define DT_FIFO	   1
#define DT_CHR	   2
#define DT_DIR	   4
#define DT_BLK	   6
#define DT_REG	   8
#define DT_LNK	   10
#define DT_SOCK	   12

typedef int (*filldir_t)(void *ctx, const char *name, size_t namelen,
			 uint64_t ino, uint8_t type);

struct super_operations {
	int (*read_inode)(struct inode *inode);
	int (*write_inode)(struct inode *inode);
	void (*evict_inode)(struct inode *inode);
	int (*sync_fs)(struct super_block *sb);
};

struct inode_operations {
	struct dentry *(*lookup)(struct inode *dir, struct dentry *dentry);
	int (*create)(struct inode *dir, struct dentry *dentry,
		      uint32_t mode);
	int (*link)(struct dentry *old_dentry, struct inode *dir,
		    struct dentry *new_dentry);
	int (*unlink)(struct inode *dir, struct dentry *dentry);
	int (*mkdir)(struct inode *dir, struct dentry *dentry, uint32_t mode);
	int (*rmdir)(struct inode *dir, struct dentry *dentry);
	int (*readlink)(struct inode *inode, char *buf, size_t size);
};

struct file_operations {
	ssize_t (*read)(struct file *file, char *buf, size_t count);
	ssize_t (*write)(struct file *file, const char *buf, size_t count);
	loff_t (*llseek)(struct file *file, loff_t offset, int whence);
	int (*open)(struct inode *inode, struct file *file);
	int (*readdir)(struct file *file, void *ctx, filldir_t filldir);
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
	dev_t i_rdev;
	int i_refcount;
	struct super_block *i_sb;
	const struct inode_operations *i_op;
	const struct file_operations *i_fop;
	void *i_private;
	struct list_head i_hash;
	struct list_head i_sb_list;
};

struct dentry {
	char d_name[VFS_NAME_MAX + 1];
	uint8_t d_namelen;
	int d_refcount;
	struct inode *d_inode;
	struct dentry *d_parent;
	struct super_block *d_sb;
	void *d_fsdata;
	struct list_head d_hash;
	struct list_head d_child;
	struct list_head d_subdirs;
};

struct file {
	const struct file_operations *f_op;
	struct dentry *f_dentry;
	struct inode *f_inode;
	void *private_data;
	loff_t f_pos;
	uint32_t f_flags;
	uint32_t f_mode;
	int refcount;
	bool static_file;
};

int vfs_open(const char *path, uint32_t flags, uint32_t mode);
ssize_t vfs_read(struct file *file, char *buf, size_t count);
ssize_t vfs_write(struct file *file, const char *buf, size_t count);
loff_t vfs_llseek(struct file *file, loff_t offset, int whence);
int vfs_readdir(struct file *file, void *ctx, filldir_t filldir);

#endif

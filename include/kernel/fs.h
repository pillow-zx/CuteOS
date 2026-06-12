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

#include <kernel/types.h>

struct task_struct;
struct trap_frame;
struct file;

#define KERN_STDIN  0
#define KERN_STDOUT 1
#define KERN_STDERR 2

#define NR_OPEN 32

#define FMODE_READ  0x1
#define FMODE_WRITE 0x2

struct file_operations {
	ssize_t (*read)(struct file *file, char *buf, size_t count);
	ssize_t (*write)(struct file *file, const char *buf, size_t count);
	loff_t (*llseek)(struct file *file, loff_t offset, int whence);
	int (*release)(struct file *file);
};

struct file {
	const struct file_operations *f_op;
	void *private_data;
	loff_t f_pos;
	uint32_t f_flags;
	uint32_t f_mode;
	int refcount;
	bool static_file;
};

struct file *file_alloc(const struct file_operations *f_op, uint32_t mode,
			void *private_data);
void file_get(struct file *file);
void file_put(struct file *file);

int fd_alloc(struct file *file);
struct file *fd_get(int fd);
int fd_close(int fd);
int fd_dup(int oldfd);
int fd_dup2(int oldfd, int newfd);

int copy_files(struct task_struct *child);
void close_files(struct task_struct *task);
void file_install_standard_fds(struct task_struct *task);

ssize_t sys_read(struct trap_frame *tf);
ssize_t sys_write(struct trap_frame *tf);
ssize_t sys_close(struct trap_frame *tf);
ssize_t sys_dup(struct trap_frame *tf);
ssize_t sys_dup3(struct trap_frame *tf);

#endif

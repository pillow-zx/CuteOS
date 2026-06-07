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

#endif

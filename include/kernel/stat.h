#ifndef _CUTEOS_KERNEL_STAT_H
#define _CUTEOS_KERNEL_STAT_H

/*
 * include/kernel/stat.h - fstat 系统调用使用的文件状态结构体
 *
 * 定义 fstat/fstatat 系统调用返回的 struct stat。
 * 布局与 Linux riscv64 ABI 兼容，以确保用户空间二进制程序
 * （如 busybox）能正确解析。
 *
 * struct stat fields:
 *   st_dev   - Device number
 *   st_ino   - Inode number
 *   st_mode  - File type and permissions
 *   st_nlink - Number of hard links
 *   st_uid   - Owner user ID
 *   st_gid   - Owner group ID
 *   st_size  - File size in bytes
 *   st_atime - Last access time
 *   st_mtime - Last modification time
 *   st_ctime - Last status change time
 *
 * File type masks:
 *   S_IFMT   - Mask for file type bits
 *   S_IFREG  - Regular file
 *   S_IFDIR  - Directory
 *   S_IFCHR  - Character device
 */

#endif

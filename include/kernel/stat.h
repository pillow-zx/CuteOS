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

#include <kernel/types.h>

#define S_IFMT	 00170000
#define S_IFSOCK 0140000
#define S_IFLNK	 0120000
#define S_IFREG	 0100000
#define S_IFBLK	 0060000
#define S_IFDIR	 0040000
#define S_IFCHR	 0020000
#define S_IFIFO	 0010000

struct kstat {
	uint64_t st_dev;
	uint64_t st_ino;
	uint32_t st_mode;
	uint32_t st_nlink;
	uint32_t st_uid;
	uint32_t st_gid;
	uint64_t st_rdev;
	uint64_t __pad1;
	int64_t st_size;
	uint32_t st_blksize;
	uint32_t __pad2;
	uint64_t st_blocks;
	int64_t st_atime_sec;
	uint64_t st_atime_nsec;
	int64_t st_mtime_sec;
	uint64_t st_mtime_nsec;
	int64_t st_ctime_sec;
	uint64_t st_ctime_nsec;
	uint32_t st_unused[2];
};

#endif

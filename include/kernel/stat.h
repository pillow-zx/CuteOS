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
#include <kernel/compiler.h>

#define S_IFMT	 00170000
#define S_IFSOCK 0140000
#define S_IFLNK	 0120000
#define S_IFREG	 0100000
#define S_IFBLK	 0060000
#define S_IFDIR	 0040000
#define S_IFCHR	 0020000
#define S_IFIFO	 0010000

#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)

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

/*
 * struct kstat 必须与 Linux riscv64 的 struct stat ABI 逐字节一致：
 * sys_fstat/sys_newfstatat 直接把它 copy_to_user 给用户态。用户态对应
 * 布局见 user/include/user.h 的 struct stat，二者不能独立漂移。
 */
static_assert(sizeof(struct kstat) == 128,
	      "struct kstat must match the riscv64 stat ABI (128 bytes)");
static_assert(offsetof(struct kstat, st_mode) == 16,
	      "st_mode offset drifted from riscv64 stat ABI");
static_assert(offsetof(struct kstat, st_size) == 48,
	      "st_size offset drifted from riscv64 stat ABI");
static_assert(offsetof(struct kstat, st_blocks) == 64,
	      "st_blocks offset drifted from riscv64 stat ABI");

#endif

#ifndef _CUTEOS_UAPI_STAT_H
#define _CUTEOS_UAPI_STAT_H

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

#define MINORBITS 20u
#define MKDEV(major, minor)                                                    \
	(((unsigned long)(major) << MINORBITS) | (unsigned long)(minor))
#define MAJOR(dev) ((unsigned int)((unsigned long)(dev) >> MINORBITS))
#define MINOR(dev)                                                             \
	((unsigned int)((unsigned long)(dev) & ((1UL << MINORBITS) - 1)))

struct stat {
	unsigned long st_dev;
	unsigned long st_ino;
	unsigned int st_mode;
	unsigned int st_nlink;
	unsigned int st_uid;
	unsigned int st_gid;
	unsigned long st_rdev;
	unsigned long __pad1;
	long st_size;
	unsigned int st_blksize;
	unsigned int __pad2;
	unsigned long st_blocks;
	long st_atime_sec;
	unsigned long st_atime_nsec;
	long st_mtime_sec;
	unsigned long st_mtime_nsec;
	long st_ctime_sec;
	unsigned long st_ctime_nsec;
	unsigned int st_unused[2];
};

struct statx_timestamp {
	long tv_sec;
	unsigned int tv_nsec;
	int __reserved;
};

struct statx {
	unsigned int stx_mask;
	unsigned int stx_blksize;
	unsigned long stx_attributes;
	unsigned int stx_nlink;
	unsigned int stx_uid;
	unsigned int stx_gid;
	unsigned short stx_mode;
	unsigned short __spare0[1];
	unsigned long stx_ino;
	unsigned long stx_size;
	unsigned long stx_blocks;
	unsigned long stx_attributes_mask;
	struct statx_timestamp stx_atime;
	struct statx_timestamp stx_btime;
	struct statx_timestamp stx_ctime;
	struct statx_timestamp stx_mtime;
	unsigned int stx_rdev_major;
	unsigned int stx_rdev_minor;
	unsigned int stx_dev_major;
	unsigned int stx_dev_minor;
	unsigned long stx_mnt_id;
	unsigned int stx_dio_mem_align;
	unsigned int stx_dio_offset_align;
	unsigned long stx_subvol;
	unsigned long __spare3[11];
};

#define STATX_TYPE	  0x00000001U
#define STATX_MODE	  0x00000002U
#define STATX_NLINK	  0x00000004U
#define STATX_UID	  0x00000008U
#define STATX_GID	  0x00000010U
#define STATX_ATIME	  0x00000020U
#define STATX_MTIME	  0x00000040U
#define STATX_CTIME	  0x00000080U
#define STATX_INO	  0x00000100U
#define STATX_SIZE	  0x00000200U
#define STATX_BLOCKS	  0x00000400U
#define STATX_BASIC_STATS 0x000007ffU
#define STATX_BTIME	  0x00000800U
#define STATX__RESERVED  0x80000000U

#endif

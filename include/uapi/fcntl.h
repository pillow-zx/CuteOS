#ifndef _CUTEOS_UAPI_FCNTL_H
#define _CUTEOS_UAPI_FCNTL_H

/**
 * @file fcntl.h
 * @brief Linux openat/fcntl/access/rename/splice flag constants.
 *
 * These numeric values are UAPI. cuteOS may implement only documented subsets
 * of the Linux flag semantics, but the bit assignments must remain compatible
 * with riscv64 userspace.
 */

#define AT_FDCWD	    -100
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR	    0x200
#define AT_EACCESS	    0x200
#define AT_SYMLINK_FOLLOW   0x400
#define AT_EMPTY_PATH	    0x1000

#define AT_STATX_SYNC_TYPE  0x6000
#define AT_STATX_FORCE_SYNC 0x2000
#define AT_STATX_DONT_SYNC  0x4000

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

#define O_RDONLY    00000000
#define O_WRONLY    00000001
#define O_RDWR	    00000002
#define O_ACCMODE   00000003
#define O_CREAT	    00000100
#define O_EXCL	    00000200
#define O_TRUNC	    00001000
#define O_APPEND    00002000
#define O_NONBLOCK  00004000
#define O_DSYNC	    00010000
#define FASYNC	    00020000
#define O_DIRECT    00040000
#define O_DIRECTORY 00200000
#define O_NOATIME   01000000
#define O_CLOEXEC   02000000
#define __O_SYNC    04000000
#define O_SYNC	    (__O_SYNC | O_DSYNC)

#define F_DUPFD		0
#define F_GETFD		1
#define F_SETFD		2
#define F_GETFL		3
#define F_SETFL		4
#define F_DUPFD_CLOEXEC 1030
#define FD_CLOEXEC	1

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define FALLOC_FL_KEEP_SIZE 0x01

#define SPLICE_F_MOVE	   1
#define SPLICE_F_NONBLOCK 2
#define SPLICE_F_MORE	   4
#define SPLICE_F_GIFT	   8

#define RENAME_NOREPLACE 0x0001

#endif

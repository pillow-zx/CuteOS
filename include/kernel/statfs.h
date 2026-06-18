#ifndef _CUTEOS_KERNEL_STATFS_H
#define _CUTEOS_KERNEL_STATFS_H

#include <kernel/types.h>

struct kstatfs {
	int64_t f_type;
	int64_t f_bsize;
	uint64_t f_blocks;
	uint64_t f_bfree;
	uint64_t f_bavail;
	uint64_t f_files;
	uint64_t f_ffree;
	int32_t f_fsid[2];
	int64_t f_namelen;
	int64_t f_frsize;
	int64_t f_flags;
	int64_t f_spare[4];
};

#endif

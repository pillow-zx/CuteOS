#ifndef _CUTEOS_UAPI_STATFS_H
#define _CUTEOS_UAPI_STATFS_H

struct statfs64 {
	long f_type;
	long f_bsize;
	unsigned long f_blocks;
	unsigned long f_bfree;
	unsigned long f_bavail;
	unsigned long f_files;
	unsigned long f_ffree;
	int f_fsid[2];
	long f_namelen;
	long f_frsize;
	long f_flags;
	long f_spare[4];
};

#endif

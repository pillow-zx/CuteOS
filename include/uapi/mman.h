#ifndef _CUTEOS_UAPI_MMAN_H
#define _CUTEOS_UAPI_MMAN_H

/**
 * @file mman.h
 * @brief Linux mmap/mprotect/mremap/msync/madvise UAPI constants.
 */

/** @def PROT_READ Mapping permits loads. */
#define PROT_READ  0x1
/** @def PROT_WRITE Mapping permits stores. */
#define PROT_WRITE 0x2
/** @def PROT_EXEC Mapping permits instruction fetch. */
#define PROT_EXEC  0x4
/** @def PROT_NONE Mapping has no user access permissions. */
#define PROT_NONE  0x0

/** @def MAP_SHARED File mapping is shared with other mappings. */
#define MAP_SHARED    0x01
/** @def MAP_PRIVATE Mapping is private to this address space. */
#define MAP_PRIVATE   0x02
/** @def MAP_FIXED Requested mmap address must be used exactly. */
#define MAP_FIXED     0x10
/** @def MAP_ANONYMOUS Mapping is not backed by a file descriptor. */
#define MAP_ANONYMOUS 0x20

/** @def MREMAP_MAYMOVE mremap may move the mapping. */
#define MREMAP_MAYMOVE	   1
/** @def MREMAP_FIXED mremap target address is supplied explicitly. */
#define MREMAP_FIXED	   2
/** @def MREMAP_DONTUNMAP Linux flag reserving old mapping on move. */
#define MREMAP_DONTUNMAP 4

/** @def MS_ASYNC Request asynchronous file mapping writeback. */
#define MS_ASYNC      1
/** @def MS_INVALIDATE Request invalidation of other mappings. */
#define MS_INVALIDATE 2
/** @def MS_SYNC Request synchronous file mapping writeback. */
#define MS_SYNC       4

/** @def MADV_NORMAL Default access-pattern advice. */
#define MADV_NORMAL	0
/** @def MADV_RANDOM Random access-pattern advice. */
#define MADV_RANDOM	1
/** @def MADV_SEQUENTIAL Sequential access-pattern advice. */
#define MADV_SEQUENTIAL 2
/** @def MADV_WILLNEED Prefetch/will-need advice. */
#define MADV_WILLNEED	3
/** @def MADV_DONTNEED Discard resident pages when supported. */
#define MADV_DONTNEED	4
/** @def MADV_FREE Lazy free advice. */
#define MADV_FREE	8
/** @def MADV_REMOVE Remove backing store for a range. */
#define MADV_REMOVE	9

#endif

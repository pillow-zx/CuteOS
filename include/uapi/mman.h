#ifndef _CUTEOS_UAPI_MMAN_H
#define _CUTEOS_UAPI_MMAN_H

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#define PROT_NONE  0x0

#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

/* madvise advice values (Linux riscv64 ABI) */
#define MADV_NORMAL	0
#define MADV_RANDOM	1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED	3
#define MADV_DONTNEED	4
#define MADV_FREE	8
#define MADV_REMOVE	9

#endif

#ifndef _CUTEOS_KERNEL_TYPES_H
#define _CUTEOS_KERNEL_TYPES_H

/*
 * include/kernel/types.h - 定宽整数类型与基础宏
 *
 * 定义内核基础类型别名。不使用标准库头文件，所有类型自行定义。
 * 设计上整个代码库统一使用定宽类型（uint8_t、uint32_t 等），
 * 不使用裸 unsigned int / unsigned long，确保在所有编译环境下
 * 类型大小一致。
 *
 * Types:
 *   uint8_t / uint16_t / uint32_t / uint64_t  - Unsigned fixed-width
 *   int8_t / int16_t / int32_t / int64_t      - Signed fixed-width
 *   size_t / ssize_t                          - Size / pointer-diff types
 *   uintptr_t / ptrdiff_t                     - Pointer-sized int types
 *   bool / true / false                       - Boolean (int-based)
 *   NULL / nullptr                            - Null pointer constant
 */

typedef signed char             int8_t;
typedef signed short            int16_t;
typedef signed int              int32_t;
typedef signed long long        int64_t;

typedef unsigned char           uint8_t;
typedef unsigned short          uint16_t;
typedef unsigned int            uint32_t;
typedef unsigned long long      uint64_t;

typedef unsigned long           uintptr_t;
typedef signed long             ptrdiff_t;

typedef unsigned long		size_t;
typedef signed long             ssize_t;

typedef _Bool                   bool;
typedef int64_t                 loff_t;
typedef uint32_t                pid_t;
typedef uint32_t                uid_t;
typedef uint32_t                gid_t;
typedef uint32_t                dev_t;

enum {
        false = 0,
        true = 1,
};

#define auto __auto_type

#define NULL ((void *)0)
#define nullptr ((void *)0)

#define INT8_MIN   (-1-0x7f)
#define INT16_MIN  (-1-0x7fff)
#define INT32_MIN  (-1-0x7fffffff)
#define INT64_MIN  (-1-0x7fffffffffffffff)

#define INT8_MAX   (0x7f)
#define INT16_MAX  (0x7fff)
#define INT32_MAX  (0x7fffffff)
#define INT64_MAX  (0x7fffffffffffffff)

#define UINT8_MAX  (0xff)
#define UINT16_MAX (0xffff)
#define UINT32_MAX (0xffffffffu)
#define UINT64_MAX (0xffffffffffffffffu)

#endif

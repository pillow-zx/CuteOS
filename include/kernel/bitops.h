#ifndef _CUTEOS_KERNEL_BITOPS_H
#define _CUTEOS_KERNEL_BITOPS_H

/*
 * include/kernel/bitops.h - 位操作工具
 *
 * 提供 buddy 分配器、调度器位图及其他通过位图管理资源的子系统
 * 所使用的位级辅助函数。
 *
 * 宏：
 *   BIT(n)      - 生成第 n 位被置位的掩码（1UL << n）
 *   GENMASK(h,l)- 生成从位 l 到位 h 的连续位掩码
 *
 * 函数：
 *   ffz(x) - 查找 x 中第一个零位（返回位索引）
 *   fls(x) - 查找 x 中最后一个置位（返回基于 1 的位置，x==0 返回 0）
 */

#include <kernel/types.h>
#include <kernel/compiler.h>

#define BIT(n)          (1UL << (n))
#define BIT_U8(n)       ((uint8_t)1U << (n))
#define BIT_U32(n)      ((uint32_t)1U << (n))
#define BIT_U64(n)      ((uint64_t)1ULL << (n))

#define GENMASK(h, l)                                                          \
        (((~0UL) << (l)) & (~0UL >> ((sizeof(unsigned long) * 8) - 1 - (h))))

#define set_bit(x, n) ((x) |= BIT(n))
#define clr_bit(x, n) ((x) &= ~BIT(n))
#define flip_bit(x, n) ((x) ^= BIT(n))
#define test_bit(x, n) (!!((x) & BIT(n)))

#define MASK(n) (BIT(n) - 1)
#define BITS(x, hi, lo)                                                        \
        ({                                                                     \
                static_assert((hi) >= (lo), "BITS: hi must be >= lo");         \
                static_assert((lo) >= 0, "BITS: lo must be >= 0");             \
                (((x) >> (lo)) & MASK((hi) - (lo) + 1));                       \
        })

#define __ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))

#define ALIGN_UP(x, a)                                                         \
        ({                                                                     \
                typeof(x) _x = (x);                                            \
                typeof(a) _a = (a);                                            \
                static_assert(constant_p(_a) ? ((_a) != 0) : 1,                \
                              "ALIGN_UP: alignment must be non-zero");         \
                static_assert(constant_p(_a) ? (((_a) & ((_a) - 1)) == 0) : 1, \
                              "ALIGN_UP: alignment must be a power of two");   \
                __ALIGN_MASK(_x, _a - 1);                                      \
        })

#define ALIGN_DOWN(x, a)                                                       \
        ({                                                                     \
                typeof(x) _x = (x);                                            \
                typeof(a) _a = (a);                                            \
                static_assert(constant_p(_a) ? ((_a) != 0) : 1,                \
                              "ALIGN_DOWN: alignment must be non-zero");       \
                static_assert(constant_p(_a) ? (((_a) & ((_a) - 1)) == 0) : 1, \
                              "ALIGN_DOWN: alignment must be a power of two"); \
                _x & ~(_a - 1);                                                \
        })

#define IS_ALIGNED(x, a)                                                       \
        ({                                                                     \
                typeof(x) _x = (x);                                            \
                typeof(a) _a = (a);                                            \
                static_assert(constant_p(_a) ? ((_a) != 0) : 1,                \
                              "IS_ALIGNED: alignment must be non-zero");       \
                static_assert(constant_p(_a) ? (((_a) & ((_a) - 1)) == 0) : 1, \
                              "IS_ALIGNED: alignment must be a power of two"); \
                ((_x & (_a - 1)) == 0);                                        \
        })

static inline int32_t ffz(uint64_t x)
{
	if (~x == 0)
		return 64;
        return ctzll(~x);
}

static inline int32_t fls(uint64_t x)
{
        if (!x)
                return 0;
        return 64UL - clzll(x);
}

#endif

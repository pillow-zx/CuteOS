/*
 * lib/softfloat.c - 提供 soft-float ABI 下的 libgcc 辅助函数
 *
 * 内核使用 -mabi=lp64 (soft-float)，而系统的 libgcc 是为 lp64d
 * (double-float) 编译的，无法直接链接。此文件手动实现内核中
 * __builtin_clzll / __builtin_ctzll / __builtin_ffsll 等
 * 内建函数在 RISC-V lp64 上所需的底层辅助例程。
 *
 * 在 RISC-V lp64 上，编译器对非常量参数的 builtin 调用映射关系：
 *   __builtin_clz/clzl/clzll  → __clzdi2
 *   __builtin_ctz/ctzl/ctzll  → __ctzdi2
 *   __builtin_ffsll/ffsl      → __ffsdi2
 *   __builtin_ffs (32-bit)    → ffs
 *
 * 提供的函数：
 *   __clzdi2(x)  - 计算 uint64_t 中前导零数量 (count leading zeros)
 *   __ctzdi2(x)  - 计算 uint64_t 中尾部零数量 (count trailing zeros)
 *   __ffsdi2(x)  - 查找 uint64_t 中第一个置位位 (find first set)
 *   ffs(x)       - 查找 uint32_t 中第一个置位位 (find first set, 32-bit)
 */

#include <kernel/libgcc.h>

/* ---- __ctzdi2 ---- */

/**
 * __ctzdi2 - count trailing zeros for 64-bit integer
 * @x: 输入值（必须非零）
 */
int __ctzdi2(uint64_t x)
{
	int count = 0;

	if ((x & 0xFFFFFFFF) == 0) {
		count += 32;
		x >>= 32;
	}
	if ((x & 0xFFFF) == 0) {
		count += 16;
		x >>= 16;
	}
	if ((x & 0xFF) == 0) {
		count += 8;
		x >>= 8;
	}
	if ((x & 0xF) == 0) {
		count += 4;
		x >>= 4;
	}
	if ((x & 0x3) == 0) {
		count += 2;
		x >>= 2;
	}
	if ((x & 0x1) == 0)
		count += 1;

	return count;
}

/* ---- __clzdi2 ---- */

/**
 * __clzdi2 - count leading zeros for 64-bit integer
 * @x: 输入值（必须非零）
 */
int __clzdi2(uint64_t x)
{
	int count = 0;

	if ((x >> 32) == 0)
		count += 32;
	else
		x >>= 32;

	if ((x >> 16) == 0)
		count += 16;
	else
		x >>= 16;

	if ((x >> 8) == 0)
		count += 8;
	else
		x >>= 8;

	if ((x >> 4) == 0)
		count += 4;
	else
		x >>= 4;

	if ((x >> 2) == 0)
		count += 2;
	else
		x >>= 2;

	if ((x >> 1) == 0)
		count += 1;

	return count;
}

/* ---- __ffsdi2 ---- */

/**
 * __ffsdi2 - find first set bit in 64-bit integer (1-based)
 * @x: 输入值
 *
 * 返回最低置位位的位置（从 1 开始），x==0 返回 0。
 */
int __ffsdi2(uint64_t x)
{
	if (x == 0)
		return 0;
	return __ctzdi2(x) + 1;
}

/* ---- ffs (32-bit) ---- */

/**
 * ffs - find first set bit in 32-bit integer (1-based)
 * @x: 输入值
 *
 * 返回最低置位位的位置（从 1 开始），x==0 返回 0。
 */
int ffs(uint32_t x)
{
	if (x == 0)
		return 0;

	int count = 1;

	if ((x & 0xFFFF) == 0) {
		count += 16;
		x >>= 16;
	}
	if ((x & 0xFF) == 0) {
		count += 8;
		x >>= 8;
	}
	if ((x & 0xF) == 0) {
		count += 4;
		x >>= 4;
	}
	if ((x & 0x3) == 0) {
		count += 2;
		x >>= 2;
	}
	if ((x & 0x1) == 0)
		count += 1;

	return count;
}

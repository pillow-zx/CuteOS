#ifndef _CUTEOS_COMPILER_BUILTIN_H
#define _CUTEOS_COMPILER_BUILTIN_H

/*
 * include/compiler/compiler_builtin.h - compiler builtin 宏
 *
 * 功能：
 *   封装 GCC/Clang 内建函数（builtins）为内核友好宏。主要用于
 *   分支预测优化、不可达代码标记、编译期常量控制等场景。
 *
 * 主要定义：
 *   likely(x)      - 封装 __builtin_expect(!!(x), 1)，提示 CPU
 *                    分支预测器该分支很可能被执行
 *   unlikely(x)    - 封装 __builtin_expect(!!(x), 0)，提示该分支
 *                    很可能不被执行
 *   unreachable()  - 封装 __builtin_unreachable()，标记代码不可达
 *   prefetch(x, rw, locality) - 封装 __builtin_prefetch，预取数据到缓存
 *   __return_address  - 返回当前函数的调用地址（__builtin_return_address）
 *   __frame_address   - 返回当前栈帧地址（__builtin_frame_address）
 */

#define likely(x)		  __builtin_expect(!!(x), 1)
#define unlikely(x)		  __builtin_expect(!!(x), 0)
#define unreachable()		  __builtin_unreachable()
#define offsetof(t, d)		  __builtin_offsetof(t, d)
#define prefetch(x, rw, locality) __builtin_prefetch(x, rw, locality)
#define __return_address()	  __builtin_return_address(0)
#define __frame_address()	  __builtin_frame_address(0)

#define ffs(x)			__builtin_ffs(x)
#define ffsl(x)			__builtin_ffsl(x)
#define ffsll(x)		__builtin_ffsll(x)
#define clz(x)			__builtin_clz(x)
#define clzl(x)			__builtin_clzl(x)
#define clzll(x)		__builtin_clzll(x)
#define ctz(x)			__builtin_ctz(x)
#define ctzl(x)			__builtin_ctzl(x)
#define ctzll(x)		__builtin_ctzll(x)

#define popcount(x)		__builtin_popcount(x)
#define popcountl(x)		__builtin_popcountl(x)
#define popcountll(x)		__builtin_popcountll(x)

#define constant_p(exp)		__builtin_constant_p(exp)

#define types_compatible(a, b)                                                 \
	__builtin_types_compatible_p(typeof(a), typeof(b))

#endif

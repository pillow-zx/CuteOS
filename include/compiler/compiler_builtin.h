#ifndef _CUTEOS_COMPILER_BUILTIN_H
#define _CUTEOS_COMPILER_BUILTIN_H

/*
 * include/compiler/compiler_builtin.h - compiler builtin 宏
 *
 * 功能：
 *   封装 GCC/Clang 内建函数（builtins）为内核友好宏。主要用于
 *   分支预测优化、不可达代码标记、编译期常量控制等场景。
 */

#define likely(x)		  __builtin_expect(!!(x), 1)
#define unlikely(x)		  __builtin_expect(!!(x), 0)
#define unreachable()		  __builtin_unreachable()
#define offsetof(t, d)		  __builtin_offsetof(t, d)
#define prefetch(x, rw, locality) __builtin_prefetch(x, rw, locality)
#define __return_address()	  __builtin_return_address(0)
#define __frame_address()	  __builtin_frame_address(0)
#define ffs(x)			  __builtin_ffs(x)
#define ffsl(x)			  __builtin_ffsl(x)
#define ffsll(x)		  __builtin_ffsll(x)
#define clz(x)			  __builtin_clz(x)
#define clzl(x)			  __builtin_clzl(x)
#define clzll(x)		  __builtin_clzll(x)
#define ctz(x)			  __builtin_ctz(x)
#define ctzl(x)			  __builtin_ctzl(x)
#define ctzll(x)		  __builtin_ctzll(x)
#define popcount(x)		  __builtin_popcount(x)
#define popcountl(x)		  __builtin_popcountl(x)
#define popcountll(x)		  __builtin_popcountll(x)

#define alignof(x)		  __alignof__(x)

#define constant_p(exp) __builtin_constant_p(exp)

#define compiletime_choose(cond, true_expr, false_expr)                        \
	__builtin_choose_expr((cond), (true_expr), (false_expr))

#define types_compatible(a, b)                                                 \
	__builtin_types_compatible_p(__typeof__(a), __typeof__(b))

#define ATOMIC_RELAXED __ATOMIC_RELAXED /* 0 */
#define ATOMIC_CONSUME __ATOMIC_CONSUME /* 1 */
#define ATOMIC_ACQUIRE __ATOMIC_ACQUIRE /* 2 */
#define ATOMIC_RELEASE __ATOMIC_RELEASE /* 3 */
#define ATOMIC_ACQ_REL __ATOMIC_ACQ_REL /* 4 */
#define ATOMIC_SEQ_CST __ATOMIC_SEQ_CST /* 5 */

#define atomic_load_n(ptr, memorder)	   __atomic_load_n(ptr, memorder)
#define atomic_store_n(ptr, val, memorder) __atomic_store_n(ptr, val, memorder)
#define atomic_add_fetch(ptr, val, memorder)                                   \
	__atomic_add_fetch(ptr, val, memorder)
#define atomic_compare_exchange_n(ptr, expected, desired, weak, succ_mo,       \
				  fail_mo)                                     \
	__atomic_compare_exchange_n(ptr, expected, desired, weak, succ_mo,     \
				    fail_mo)

#define memcpy(dst, src, n)	    __builtin_memcpy((dst), (src), (n))
#define memset(dst, c, n)	    __builtin_memset((dst), (c), (n))
#define memcmp(lhs, rhs, n)	    __builtin_memcmp((lhs), (rhs), (n))
#define memmove(dst, src, n)	    __builtin_memmove((dst), (src), (n))
#define strlen(s)		    __builtin_strlen((s))
#define strnlen(s, maxlen)	    __builtin_strnlen((s), (maxlen))
#define strcmp(lhs, rhs)	    __builtin_strcmp((lhs), (rhs))
#define strncmp(lhs, rhs, n)	    __builtin_strncmp((lhs), (rhs), (n))
#define strcpy(dst, src)	    __builtin_strcpy((dst), (src))
#define strncpy(dst, src, n)	    __builtin_strncpy((dst), (src), (n))
#define strchr(s, c)		    __builtin_strchr((s), (c))
#define strrchr(s, c)		    __builtin_strrchr((s), (c))

#endif

#ifndef _CUTEOS_COMPILER_BUILTIN_H
#define _CUTEOS_COMPILER_BUILTIN_H

/*
 * include/compiler/compiler_builtin.h - compiler builtin 宏
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

#define alignof(x) __alignof__(x)

#define constant_p(exp) __builtin_constant_p(exp)

#define compiletime_choose(cond, true_expr, false_expr)                        \
	__builtin_choose_expr((cond), (true_expr), (false_expr))

#define types_compatible(a, b)                                                 \
	__builtin_types_compatible_p(__typeof__(a), __typeof__(b))

#define ATOMIC_RELAXED __ATOMIC_RELAXED
#define ATOMIC_CONSUME __ATOMIC_CONSUME
#define ATOMIC_ACQUIRE __ATOMIC_ACQUIRE
#define ATOMIC_RELEASE __ATOMIC_RELEASE
#define ATOMIC_ACQ_REL __ATOMIC_ACQ_REL
#define ATOMIC_SEQ_CST __ATOMIC_SEQ_CST

#define atomic_load_n(ptr, memorder)	   __atomic_load_n(ptr, memorder)
#define atomic_store_n(ptr, val, memorder) __atomic_store_n(ptr, val, memorder)
#define atomic_add_fetch(ptr, val, memorder)                                   \
	__atomic_add_fetch(ptr, val, memorder)
#define atomic_compare_exchange_n(ptr, expected, desired, weak, succ_mo,       \
				  fail_mo)                                     \
	__atomic_compare_exchange_n(ptr, expected, desired, weak, succ_mo,     \
				    fail_mo)

#if __has_builtin(__builtin_memcpy)
#define memcpy(dst, src, n) __builtin_memcpy((dst), (src), (n))
#else
extern void *memcpy(void *restrict dst, const void *restrict src,
		    unsigned long n);
#define memcpy(dst, src, n) memcpy(dst, src, n)
#endif

#if __has_builtin(__builtin_memset)
#define memset(dst, c, n) __builtin_memset((dst), (c), (n))
#else
extern void *memset(void *dst, int c, unsigned long n);
#define memset(dst, c, n) memset((dst), (c), (n))
#endif

#if __has_builtin(__builtin_memcpy)
#define memcmp(lhs, rhs, n) __builtin_memcmp((lhs), (rhs), (n))
#else
extern int memcmp(const void *lsh, const void *rhs, unsigned long n);
#define memcmp(lhs, rhs, n) memcmp((lhs), (rhs), (n))
#endif

#if __has_builtin(__builtin_memmove)
#define memmove(dst, src, n) __builtin_memmove((dst), (src), (n))
#else
extern void *memmove(void *dst, const void *src, unsigned long n);
#define memmove(dst, src, n) memmove((dst), (src), (n))
#endif

#if __has_builtin(__builtin_strlen)
#define strlen(s) __builtin_strlen((s))
#else
extern unsigned long strlen(const char *s);
#define strlen(s) strlen((s))
#endif

#if __has_builtin(__builtin_strnlen)
#define strnlen(s, maxlen) __builtin_strnlen((s), (maxlen))
#else
extern unsigned long strnlen(const char *s, const unsigned long maxlen);
#define strnlen(s, maxlen) strnlen(s, maxlen)
#endif

#if __has_builtin(__builtin_strcmp)
#define strcmp(lhs, rhs) __builtin_strcmp((lhs), (rhs))
#else
extern int strcmp(const char *lhs, const char *rhs);
#define strcmp(lhs, rhs) strcmp(lhs, rhs)
#endif

#if __has_builtin(__builtin_strncmp)
#define strncmp(lhs, rhs, n) __builtin_strncmp((lhs), (rhs), (n))
#else
extern int strncmp(const char *lhs, const char *rhs, unsigned long n);
#define strncmp(lhs, rhs, n) strncmp((lhs), (rhs), (n))
#endif

#if __has_builtin(__builtin_strcpy)
#define strcpy(dst, src) __builtin_strcpy((dst), (src))
#else
extern char *strcpy(char *restrict dst, const char *restrict src);
#define strcpy(dst, src) strcpy((dst), (src))
#endif

#if __has_builtin(__builtin_strncpy)
#define strncpy(dst, src, n) __builtin_strncpy((dst), (src), (n))
#else
extern char *strncpy(char *restrict dst, const char *restrict src,
		     unsigned long n);
#define strncpy(dst, src, n) strncpy((dst), (src), (n))
#endif

#if __has_builtin(__builtin_strchr)
#define strchr(s, c) __builtin_strchr((s), (c))
#else
extern char *strchr(const char *s, int c);
#define strchr(s, c) strchr((s), (c))
#endif

#if __has_builtin(__builtin_strrchr)
#define strrchr(s, c) __builtin_strrchr((s), (c))
#else
extern char *strrchr(const char *s, int c);
#define strrchr(s, c) strrchr(s, c)
#endif

#endif

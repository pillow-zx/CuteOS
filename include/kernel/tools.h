/*
 * include/kernel/tools.h - 内核通用工具宏
 *
 * 功能：
 *   提供内核各子系统共享的通用宏工具，包括编译期断言、
 *   MMIO 寄存器访问、数组操作、容器结构体指针反推、
 *   常量表达式断言，以及类型安全的极值宏。
 */

#ifndef _CUTEOS_KERNEL_TOOLS_H
#define _CUTEOS_KERNEL_TOOLS_H

#include <kernel/compiler.h>

#define MMIO_READ(type, addr)	    (*(volatile type *)(addr))
#define MMIO_WRITE(type, addr, val) (*(volatile type *)(addr) = (val))

#define ISARR(arr, msg) static_assert(!same_type((arr), &(arr)[0]), msg)

#define ARRLEN(arr)                                                            \
	statement_expr(                                                        \
		ISARR(arr,                                                     \
		      "ARRLEN: argument must be an array, not an pointer");    \
		sizeof((arr)) / sizeof((arr)[0]);)

#define BUILD_BUG_ON(cond)	((void)sizeof(char[1 - 2 * !!(cond)]))
#define BUILD_BUG_ON_ZERO(cond) ((int)sizeof(char[1 - 2 * !!(cond)]) - 1)

#define typecheck(type, expr)                                                  \
	statement_expr(type __dummy; type_of(expr) __dummy2;                   \
		       (void)(&__dummy == &__dummy2); 1;)

#define typecheck_pointer(type, expr) typecheck(type *, expr)

#define container_of(ptr, type, member)                                        \
	statement_expr(                                                        \
		static_assert(same_type(*(ptr), ((type *)0)->member) ||        \
				      same_type(*(ptr), void),                 \
			      "pointer type mismatch in container_of()");      \
		(type *)((void *)((__UINTPTR_TYPE__)(ptr) -                    \
				  offsetof(type, member)));)

#define container_of_const(ptr, type, member)                                  \
	statement_expr(                                                        \
		static_assert(same_type(*(ptr), ((type *)0)->member) ||        \
				      same_type(*(ptr), void),                 \
			      "pointer type mismatch in container_of()");      \
		_Generic((ptr),                                                \
			const type_of(*(ptr)) *: (const type *)((              \
				const void *)((const char *)(ptr) -            \
					      offsetof(type, member))),        \
			default: ((                                            \
				type *)((void *)((__UINTPTR_TYPE__)(ptr) -     \
						 offsetof(type, member)))));)

#define constexpr(expr) constant_p(expr)

#define IS_POWER_OF_2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))

#define MAX(a, b)                                                              \
	statement_expr(                                                        \
		static_assert(                                                 \
			same_type(a, b),                                       \
			"MAX requires both arguments to be the same type");    \
		auto _a = (a); auto _b = (b); _a > _b ? _a : _b;)

#define MIN(a, b)                                                              \
	statement_expr(                                                        \
		static_assert(                                                 \
			same_type(a, b),                                       \
			"MIN Requires both arguments to be the same type");    \
		auto _a = (a); auto _b = (b); _a < _b ? _a : _b;)

#endif

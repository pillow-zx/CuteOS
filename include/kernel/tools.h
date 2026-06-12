/*
 * include/kernel/tools.h - 内核通用工具宏
 *
 * 功能：
 *   提供内核各子系统共享的通用宏工具，包括编译期断言、
 *   MMIO 寄存器访问、数组操作、容器结构体指针反推、
 *   常量表达式断言，以及类型安全的极值宏。
 *
 * 主要宏：
 *   static_assert(cond, msg)
 *     编译期断言。若 cond 为假则编译报错并输出 msg。
 *
 *   MMIO_READ(type, addr) / MMIO_WRITE(type, addr, val)
 *     内存映射 I/O 寄存器读写。通过 volatile 指针访问，
 *     确保编译器不会优化掉或重排序设备寄存器访问。
 *
 *   same_type(a, b)
 *     编译期判断两个表达式的类型是否兼容。
 *
 *   ISARR(arr, msg)
 *     编译期断言参数必须是数组类型，不能是指针。
 *
 *   ARRLEN(arr)
 *     编译期计算数组元素个数。使用 ISARR 防止对指针误调用。
 *
 *   container_of(ptr, type, member)
 *     已知结构体成员指针，反推包含该成员的结构体指针。
 *     编译期检查指针类型与成员类型是否匹配。
 *
 *   container_of_const(ptr, type, member)
 *     container_of 的 const 安全版本：若输入指针为 const，
 *     返回的结构体指针也保持 const 限定。
 *
 *   constexpr(expr)
 *     判断表达式是否为编译期常量。用于对常量参数启用额外的
 *     编译期检查。
 *
 *   MAX(a, b) / MIN(a, b)
 *     类型安全的极值宏。编译期检查两个参数类型一致，
 *     使用语句表达式避免双重求值问题。
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

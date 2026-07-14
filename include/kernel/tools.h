/**
 * @file tools.h
 * @brief 内核通用工具宏。
 */

#ifndef _CUTEOS_KERNEL_TOOLS_H
#define _CUTEOS_KERNEL_TOOLS_H

#include <kernel/compiler.h>

/**
 * @def MMIO_READ
 * @brief Perform a volatile typed load from an MMIO address.
 */
#define MMIO_READ(type, addr)	    (*(volatile type *)(addr))

/**
 * @def MMIO_WRITE
 * @brief Perform a volatile typed store to an MMIO address.
 */
#define MMIO_WRITE(type, addr, val) (*(volatile type *)(addr) = (val))

/**
 * @def ISARR
 * @brief Compile-time assertion that an expression is an array.
 */
#define ISARR(arr, msg) static_assert(!same_type((arr), &(arr)[0]), msg)

/**
 * @def ARRLEN
 * @brief Return the number of elements in an array expression.
 *
 * The macro rejects pointer arguments at compile time by comparing the array
 * expression type with the type of its first-element pointer.
 */
#define ARRLEN(arr)                                                            \
	statement_expr(                                                        \
		ISARR(arr,                                                     \
		      "ARRLEN: argument must be an array, not an pointer");    \
		sizeof((arr)) / sizeof((arr)[0]);)

/**
 * @def BUILD_BUG_ON
 * @brief Trigger a compile-time error when @p cond is true.
 */
#define BUILD_BUG_ON(cond)	((void)sizeof(char[1 - 2 * !!(cond)]))

/**
 * @def BUILD_BUG_ON_ZERO
 * @brief Compile-time assertion expression that evaluates to zero.
 */
#define BUILD_BUG_ON_ZERO(cond) ((int)sizeof(char[1 - 2 * !!(cond)]) - 1)

/**
 * @def typecheck
 * @brief Compile-time check that an expression has an exact type.
 */
#define typecheck(type, expr)                                                  \
	statement_expr(type __dummy; typeof(expr) __dummy2;                    \
		       (void)(&__dummy == &__dummy2); 1;)

/**
 * @def typecheck_pointer
 * @brief Compile-time check that an expression has pointer-to-type type.
 */
#define typecheck_pointer(type, expr) typecheck(type *, expr)

/**
 * @def container_of
 * @brief Recover a containing object pointer from an embedded member pointer.
 * @param ptr Pointer to @p member.
 * @param type Containing object type.
 * @param member Member name inside @p type.
 *
 * A static type check verifies that @p ptr points to the selected member type
 * unless the pointer is explicitly void-typed.
 */
#define container_of(ptr, type, member)                                        \
	statement_expr(                                                        \
		static_assert(same_type(*(ptr), ((type *)0)->member) ||        \
				      same_type(*(ptr), void),                 \
			      "pointer type mismatch in container_of()");      \
		(type *)((void *)((__UINTPTR_TYPE__)(ptr) -                    \
				  offsetof(type, member)));)

/**
 * @def container_of_const
 * @brief Const-preserving variant of @ref container_of.
 */
#define container_of_const(ptr, type, member)                                  \
	statement_expr(                                                        \
		static_assert(same_type(*(ptr), ((type *)0)->member) ||        \
				      same_type(*(ptr), void),                 \
			      "pointer type mismatch in container_of()");      \
		_Generic((ptr),                                                \
			const typeof(*(ptr)) *: (const type *)((               \
				const void *)((const char *)(ptr) -            \
					      offsetof(type, member))),        \
			default: ((                                            \
				type *)((void *)((__UINTPTR_TYPE__)(ptr) -     \
						 offsetof(type, member)))));)

/**
 * @def is_constexpr
 * @brief Test whether an expression is known constant to the compiler.
 */
#define is_constexpr(expr) constant_p(expr)

/**
 * @def IS_POWER_OF_2
 * @brief Return true when @p x is a non-zero power of two.
 */
#define IS_POWER_OF_2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))

/**
 * @def MAX
 * @brief Type-checked maximum of two same-typed expressions.
 */
#define MAX(a, b)                                                              \
	statement_expr(                                                        \
		static_assert(                                                 \
			same_type(a, b),                                       \
			"MAX requires both arguments to be the same type");    \
		auto _a = (a); auto _b = (b); _a > _b ? _a : _b;)

/**
 * @def MIN
 * @brief Type-checked minimum of two same-typed expressions.
 */
#define MIN(a, b)                                                              \
	statement_expr(                                                        \
		static_assert(                                                 \
			same_type(a, b),                                       \
			"MIN Requires both arguments to be the same type");    \
		auto _a = (a); auto _b = (b); _a < _b ? _a : _b;)

#endif

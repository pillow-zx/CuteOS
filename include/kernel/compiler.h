#ifndef _CUTEOS_KERNEL_COMPILER_H
#define _CUTEOS_KERNEL_COMPILER_H

/**
 * @file compiler.h
 * @brief Compiler extension aliases used by kernel headers.
 */

#include <compiler/compiler_attribute.h>
#include <compiler/compiler_builtin.h>

/** @def auto GNU __auto_type alias used by type-safe helper macros. */
#define auto			 __auto_type
/** @def type_of Return the GNU typeof of an expression. */
#define type_of(expr)		 __typeof__(expr)
/** @def type_of_member Return the GNU typeof of a struct/union member. */
#define type_of_member(type, m)	 __typeof__(((type *)0)->m)
/** @def statement_expr Wrap GNU statement-expression syntax. */
#define statement_expr(...)	 __extension__({__VA_ARGS__})
/** @def static_assert C11 static assertion wrapper. */
#define static_assert(cond, msg) _Static_assert(cond, msg)
/** @def same_type Test compile-time type compatibility. */
#define same_type(a, b)		 types_compatible(a, b)

#endif

#ifndef _CUTEOS_KERNEL_COMPILER_H
#define _CUTEOS_KERNEL_COMPILER_H

/*
 * include/kernel/compiler.h - 编译器抽象统一头文件
 *
 * 聚合各编译器相关的子头文件，使内核其余部分只需包含
 * <kernel/compiler.h> 即可。
 *
 * 子头文件提供：
 *   compiler/compiler_attribute.h - __packed, __aligned, __section,
 *                                   __noreturn 等
 *   compiler/compiler_builtin.h  - likely(), unlikely(), unreachable() 等
 *
 * 本文件额外提供 GNU C 语法扩展封装：
 *   auto, auto_type, type_of, statement_expr, static_assert, same_type 等
 */

#include <compiler/compiler_attribute.h>
#include <compiler/compiler_builtin.h>

#define auto			 __auto_type
#define type_of(expr)		 __typeof__(expr)
#define type_of_member(type, m)	 __typeof__(((type *)0)->m)
#define statement_expr(...)	 __extension__({__VA_ARGS__})
#define static_assert(cond, msg) _Static_assert(cond, msg)
#define same_type(a, b)		 types_compatible(a, b)

#endif

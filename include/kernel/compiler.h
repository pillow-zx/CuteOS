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
 * __noreturn 等 compiler/compiler_builtin.h  - likely(), unlikely(),
 * unreachable() 等
 *
 * 子头文件中定义的其他常用宏：
 *   container_of(ptr, type, member) - 从成员指针获取包含它的结构体
 *   constexpr(expr)                 - 编译期常量表达式断言
 */

#include <compiler/compiler_attribute.h>
#include <compiler/compiler_builtin.h>

#endif

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

#define static_assert(cond, msg) _Static_assert(cond, msg)

#define TYPESAME(a, b) types_compatible(a, b)
#define ISARR(arr, msg) static_assert(!TYPESAME((arr), (&(arr)[0])), msg)
#define ARRLEN(arr)                                                            \
        ({                                                                     \
                ISARR(arr,                                                     \
                      "ARRLEN: argument must be an array, not an pointer");    \
                sizeof((arr)) / sizeof((arr)[0]);                              \
        })

#define container_of(ptr, type, member)                                        \
        ({                                                                     \
                static_assert(TYPESAME(*(ptr), ((type *)0)->member) ||         \
                                      TYPESAME(*(ptr), void),                  \
                              "pointer type mismatch in container_of()");      \
                (type *)((void *)((__UINTPTR_TYPE__)(ptr) -                    \
                                  offsetof(type, member)));                    \
        })

#define container_of_const(ptr, type, member)                                  \
        ({                                                                     \
                static_assert(TYPESAME(*(ptr), ((type *)0)->member) ||         \
                                      TYPESAME(*(ptr), void),                  \
                              "pointer type mismatch in container_of()");      \
                _Generic((ptr),                                                \
                        const typeof(*(ptr)) *: (const type *)((               \
                                const void *)((const char *)(ptr) -            \
                                              offsetof(type, member))),        \
                        default: (                                             \
                                 (type *)((void *)((__UINTPTR_TYPE__)(ptr) -   \
                                                   offsetof(type, member))))); \
        })

#define constexpr(expr)                                                        \
        ({                                                                     \
                static_assert(constant_p(expr),                                \
                              "constexpr: requires a compile-time constant "   \
                              "expression");                                   \
                (expr)                                                         \
        })

#endif

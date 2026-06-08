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
 *   TYPESAME(a, b)
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
 *     断言表达式为编译期常量，否则编译报错。用于需要
 *     编译期求值的场景（如数组大小、switch case 等）。
 *
 *   MAX(a, b) / MIN(a, b)
 *     类型安全的极值宏。编译期检查两个参数类型一致，
 *     使用语句表达式避免双重求值问题。
 */

#ifndef _CUTEOS_KERNEL_TOOLS_H
#define _CUTEOS_KERNEL_TOOLS_H

#include <kernel/compiler.h>

#define static_assert(cond, msg) _Static_assert(cond, msg)

#define MMIO_READ(type, addr)       (*(volatile type *)(addr))
#define MMIO_WRITE(type, addr, val) (*(volatile type *)(addr) = (val))

#define TYPESAME(a, b)  types_compatible(a, b)
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

#define MAX(a, b)                                                              \
        ({                                                                     \
                static_assert(                                                 \
                        TYPESAME(a, b),                                        \
                        "MAX requires both arguments to be the same type");    \
                typeof(a) _a = (a);                                            \
                typeof(b) _b = b;                                              \
                _a > _b ? _a : _b;                                             \
        })

#define MIN(a, b)                                                              \
        ({                                                                     \
                static_assert(                                                 \
                        types_compatible(a, b),                                \
                        "MIN Requires both arguments to be the same type");    \
                typeof(a) _a = (a);                                            \
                typeof(b) _b = (b);                                            \
                _a < _b ? _a : _b;                                             \
        })

#endif

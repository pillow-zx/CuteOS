#ifndef _CUTEOS_COMPILER_ATTRIBUTE_H
#define _CUTEOS_COMPILER_ATTRIBUTE_H

/*
 * include/compiler/compiler_attribute.h - compiler attribute 宏
 *
 * 功能：
 *   封装 GCC/Clang 的 __attribute__((...)) 为简洁的内核宏。
 *   用于控制结构体布局、函数调用约定、变量/函数的段分配、
 *   以及对编译器的优化提示。
 *
 * 主要定义：
 *   __packed       - 结构体紧密排列（取消对齐填充）
 *   __aligned(n)   - 指定对齐字节数
 *   __section(s)   - 将变量/函数放入指定 ELF 段
 *   __unused       - 抑制未使用警告
 *   __used         - 强制保留符号（即使未被引用）
 *   __noreturn     - 函数不会返回
 *   __weak         - 弱符号定义
 *   __alias(name)  - 别名定义
 *   __inline       - 强制内联
 *   __noinline     - 禁止内联
 *   __printf(fmt, arg) - printf 格式字符串检查
 *   __cold         - 标记函数为冷路径（优化器可将其远离热路径）
 *   __hot          - 标记函数为热路径
 *   __maybe_unused - 可能未使用的变量/函数
 */

#define __packed                __attribute__((__packed__))
#define __aligned(x)            __attribute__((__aligned__(x)))
#define __used                  __attribute__((__used__))
#define __unused                __attribute__((__unused__))
#define __maybe_unused          __attribute__((__unused__))
#define __must_check            __attribute__((__warn_unused_result__))
#define __noreturn              __attribute__((__noreturn__))
#define __always_inline         inline __attribute__((__always_inline__))
#define __noinline              __attribute__((__noinline__))
#define __hot                   __attribute__((__hot__))
#define __cold                  __attribute__((__cold__))
#define __alias(str)            __attribute__((__alias__(str)))
#define __pure                  __attribute__((__pure__))
#define __const                 __attribute__((__const__))
#define __section(section)      __attribute__((__section__(section)))
#define __weak                  __attribute__((__weak__))
#define __printf(a, b)          __attribute__((__format__(printf, a, b)))

#endif

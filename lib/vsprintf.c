/*
 * lib/vsprintf.c - 格式化输出（printk 底层）
 *
 * 功能：
 *   实现内核中的格式化字符串核心引擎。提供 vsprintf/vsnprintf 函数，
 *   支持 printf 风格格式串解析。printk 的输出最终依赖此模块完成
 *   数值到字符串的转换。所有格式转换均在栈上完成，不依赖动态内存分配。
 *
 * 支持的格式符：
 *   %d       - 有符号十进制整数
 *   %x       - 无符号十六进制整数（小写）
 *   %s       - 字符串
 *   %c       - 字符
 *   %p       - 指针（十六进制）
 *   %%       - 转义百分号
 *   %ld      - 有符号长十进制
 *   %lu      - 无符号长十进制
 *   %llx     - 无符号长长十六进制
 *   %#x      - 带前缀的十六进制（0x）
 *   %-Ns     - 左对齐字符串（N 为宽度）
 *   %0Nd     - 零填充十进制（N 为宽度）
 *
 * 主要函数：
 *   vsprintf(buf, fmt, args)       - 将格式化结果写入缓冲区
 *   vsnprintf(buf, size, fmt, args)- 带缓冲区大小限制的格式化
 */

#include <kernel/types.h>



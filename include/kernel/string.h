#ifndef _CUTEOS_KERNEL_STRING_H
#define _CUTEOS_KERNEL_STRING_H

/*
 * string.h - 标准库内存/字符串基本函数的实现
 *
 * - memcpy/memset/memcmp/memmove 内存控制基本函数
 * - strlen/strcmp/strcpy/strchr/strrchr/strncmp/strncpy 字符串控制基本函数
 */

#include <kernel/types.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *vl, const void *vr, size_t n);
void *memmove(void *dst, const void *src, size_t n);

size_t strlen(const char *s);
int strcmp(const char *l, const char *r);
int strncmp(const char *l, const char *r, size_t n);
char *strcpy(char *restrict d, const char *restrict s);
char *strncpy(char *restrict d, const char *restrict s, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);

#endif

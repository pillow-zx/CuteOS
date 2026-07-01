#ifndef _CUTEOS_KERNEL_STRING_H
#define _CUTEOS_KERNEL_STRING_H

/*
 * string.h - 标准库内存/字符串基本函数的实现
 *
 * - memcpy/memset/memcmp/memmove 内存控制基本函数
 * - strlen/strcmp/strcpy/strchr/strrchr/strncmp/strncpy 字符串控制基本函数
 */

#include <kernel/types.h>
#include <kernel/compiler.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t n)
	__access(write_only, 1, 3) __access(read_only, 2, 3);
void *memset(void *dst, int c, size_t n) __access(write_only, 1, 3);
int memcmp(const void *vl, const void *vr, size_t n)
	__access(read_only, 1, 3) __access(read_only, 2, 3);
void *memmove(void *dst, const void *src, size_t n)
	__access(write_only, 1, 3) __access(read_only, 2, 3);
size_t strlen(const char *s) __access_no_size(read_only, 1);
size_t strnlen(const char *s, const size_t maxlen);
int strcmp(const char *l, const char *r)
	__access_no_size(read_only, 1) __access_no_size(read_only, 2);
int strncmp(const char *l, const char *r, size_t n)
	__access(read_only, 1, 3) __access(read_only, 2, 3);
char *strcpy(char *restrict d, const char *restrict s)
	__access_no_size(write_only, 1) __access_no_size(read_only, 2);
char *strncpy(char *restrict d, const char *restrict s, size_t n)
	__access(write_only, 1, 3) __access(read_only, 2, 3);
char *strchr(const char *s, int c) __access_no_size(read_only, 1);
char *strrchr(const char *s, int c) __access_no_size(read_only, 1);

#endif

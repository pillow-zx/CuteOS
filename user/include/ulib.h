#ifndef _ULIB_H
#define _ULIB_H

#include <stdarg.h>
#include <user.h>

#define NULL ((void *)0)

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int streq(const char *a, const char *b);
void strcpy(char *dst, const char *src);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
long atoi(const char *s);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int printf(const char *fmt, ...);

#endif

#ifndef _ULIB_H
#define _ULIB_H

#include <user.h>

#define NULL ((void *)0)

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int streq(const char *a, const char *b);
void strcpy(char *dst, const char *src);
void print(const char *s);
void print_hex(unsigned long val);
void print_long(long val);

#endif

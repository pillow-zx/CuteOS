/*
 * lib/string.c - 字符串与内存操作
 *
 * 功能：
 *   提供内核内部使用的标准字符串和内存操作函数实现。
 *   这些函数与 C 标准库的 string.h 功能相同，但由于内核无法链接
 *   标准库，需要内部实现。
 *
 * 主要函数：
 *   memcpy(dest, src, n)    - 内存复制
 *   memset(s, c, n)         - 内存设值
 *   memcmp(s1, s2, n)       - 内存比较
 *   memmove(dest, src, n)   - 安全内存复制（处理重叠）
 *   strlen(s)               - 字符串长度
 *   strcmp(s1, s2)          - 字符串比较
 *   strncmp(s1, s2, n)      - 有限长度字符串比较
 *   strcpy(dest, src)       - 字符串复制
 *   strncpy(dest, src, n)   - 有限长度字符串复制
 *   strchr(s, c)            - 字符串中查找字符
 *   strrchr(s, c)           - 字符串中反向查找字符
 */

#include <kernel/types.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (n--)
		*d++ = *s++;

	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	unsigned char *p = dst;

	while (n--)
		*p++ = (unsigned char)c;

	return dst;
}

int memcmp(const void *vl, const void *vr, size_t n)
{
	const unsigned char *l = vl, *r = vr;
	for (; n && *l == *r; n--, l++, r++)
		;
	return n ? *l - *r : 0;
}

void *memmove(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	if (d == s)
		return dst;

	if (d < s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;

		while (n--)
			*--d = *--s;
	}

	return dst;
}

size_t strlen(const char *s)
{
	const char *a = s;
	for (; *s; s++)
		;
	return s - a;
}

size_t strnlen(const char *s, const size_t maxlen)
{
	const char *end = s;
	size_t n = maxlen;

	while (n-- && *end)
		end++;

	return end - s;
}

int strcmp(const char *l, const char *r)
{
	for (; *l == *r && *l; l++, r++)
		;
	return *(unsigned char *)l - *(unsigned char *)r;
}

int strncmp(const char *_l, const char *_r, size_t n)
{
	const unsigned char *l = (void *)_l, *r = (void *)_r;
	if (!n--)
		return 0;
	for (; *l && *r && n && *l == *r; l++, r++, n--)
		;
	return *l - *r;
}

char *strcpy(char *restrict d, const char *restrict s)
{
	char *ret = d;

	while ((*d++ = *s++))
		;

	return ret;
}

char *strncpy(char *restrict d, const char *restrict s, size_t n)
{
	char *ret = d;

	while (n && *s) {
		*d++ = *s++;
		n--;
	}

	while (n--)
		*d++ = '\0';

	return ret;
}

char *strchr(const char *s, int c)
{
	c = (unsigned char)c;
	if (!c)
		return (char *)s + strlen(s);

	for (; *s && *(unsigned char *)s != c; s++)
		;
	char *r = (char *)s;

	return *(unsigned char *)r == (unsigned char)c ? r : 0;
}

char *strrchr(const char *s, int c)
{
	const unsigned char *m = (void *)s;
	c = (unsigned char)c;
	size_t n = strlen(s) + 1;

	while (n--) {
		if (m[n] == c)
			return (void *)(m + n);
	}
	return 0;
}

#include <ulib.h>

size_t strlen(const char *s)
{
	size_t len = 0;

	while (s[len])
		len++;
	return len;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *b && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	while (n > 0 && *a && *b && *a == *b) {
		a++;
		b++;
		n--;
	}
	if (n == 0)
		return 0;
	return (unsigned char)*a - (unsigned char)*b;
}

int streq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

void strcpy(char *dst, const char *src)
{
	while ((*dst++ = *src++) != '\0') {
	}
}

void strncpy(char *dst, const char *src, size_t n)
{
	while (n > 0 && *src) {
		*dst++ = *src++;
		n--;
	}
	while (n > 0) {
		*dst++ = '\0';
		n--;
	}
}

char *strchr(const char *s, int c)
{
	while (*s) {
		if ((unsigned char)*s == (unsigned char)c)
			return (char *)s;
		s++;
	}
	return c == 0 ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
	const char *last = NULL;

	while (*s) {
		if ((unsigned char)*s == (unsigned char)c)
			last = s;
		s++;
	}
	if (c == 0)
		return (char *)s;
	return (char *)last;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	for (size_t i = 0; i < n; i++)
		d[i] = s[i];
	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	unsigned char *d = dst;

	for (size_t i = 0; i < n; i++)
		d[i] = (unsigned char)c;
	return dst;
}

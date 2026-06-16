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

int streq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

void strcpy(char *dst, const char *src)
{
	while ((*dst++ = *src++) != '\0') {
	}
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

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

void print(const char *s)
{
	write(1, s, strlen(s));
}

void print_hex(unsigned long val)
{
	char buf[19];

	buf[0] = '0';
	buf[1] = 'x';
	for (int i = 15; i >= 0; i--) {
		int d = val & 0xf;
		buf[2 + i] = d < 10 ? '0' + d : 'a' + d - 10;
		val >>= 4;
	}
	buf[18] = '\0';
	print(buf);
}

void print_long(long val)
{
	if (val < 0) {
		print("-");
		val = -val;
	}
	print_hex((unsigned long)val);
}

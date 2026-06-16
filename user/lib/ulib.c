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

long atoi(const char *s)
{
	long val = 0;
	long sign = 1;

	while (*s == ' ' || *s == '\t')
		s++;
	if (*s == '-') {
		sign = -1;
		s++;
	}
	while (*s >= '0' && *s <= '9') {
		val = val * 10 + (*s - '0');
		s++;
	}
	return sign * val;
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

void print_dec(unsigned long val)
{
	char buf[21];
	int i = sizeof(buf) - 1;

	buf[i] = '\0';
	if (val == 0) {
		print("0");
		return;
	}

	while (val > 0 && i > 0) {
		buf[--i] = '0' + val % 10;
		val /= 10;
	}
	print(&buf[i]);
}

void print_long(long val)
{
	if (val < 0) {
		print("-");
		val = -val;
	}
	print_dec((unsigned long)val);
}

void print_error(const char *cmd, const char *arg, long err)
{
	print(cmd);
	if (arg) {
		print(": ");
		print(arg);
	}
	print(": error ");
	print_long(err);
	print("\n");
}

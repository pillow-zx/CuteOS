#include <ulib.h>

int printf(const char *fmt, ...)
{
	va_list ap;
	char buf[256];
	int len;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (len < 0)
		return len;
	if ((size_t)len >= sizeof(buf))
		len = sizeof(buf) - 1;

	write(1, buf, len);
	return len;
}

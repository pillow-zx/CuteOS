#include <ulib.h>

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

#include <ulib.h>

static char *emit(char *buf, char *end, char c)
{
	if (!end || buf < end)
		*buf = c;
	return buf + 1;
}

static char *emit_string(char *buf, char *end, const char *s)
{
	while (*s)
		buf = emit(buf, end, *s++);
	return buf;
}

static char *emit_num(char *buf, char *end, unsigned long long num, int base,
		      int upper, int neg)
{
	static const char digits_lower[] = "0123456789abcdef";
	static const char digits_upper[] = "0123456789ABCDEF";
	const char *digits = upper ? digits_upper : digits_lower;
	char tmp[32];
	int nd = 0;

	if (num == 0) {
		tmp[nd++] = '0';
	} else {
		while (num) {
			tmp[nd++] = digits[num % base];
			num /= base;
		}
	}

	if (neg)
		buf = emit(buf, end, '-');

	while (--nd >= 0)
		buf = emit(buf, end, tmp[nd]);

	return buf;
}

static int format(char *buf, char *end, const char *fmt, va_list ap)
{
	char *p = buf;

	while (*fmt) {
		unsigned long long unum;
		long long snum;

		if (*fmt != '%') {
			p = emit(p, end, *fmt++);
			continue;
		}

		fmt++;

		switch (*fmt++) {
		case '%':
			p = emit(p, end, '%');
			break;
		case 'c':
			p = emit(p, end, (char)va_arg(ap, int));
			break;
		case 's': {
			const char *s = va_arg(ap, const char *);

			if (!s)
				s = "(null)";
			p = emit_string(p, end, s);
			break;
		}
		case 'd':
			snum = va_arg(ap, int);
			unum = snum < 0 ? -(unsigned long long)snum :
					  (unsigned long long)snum;
			p = emit_num(p, end, unum, 10, 0, snum < 0);
			break;
		case 'u':
			unum = va_arg(ap, unsigned int);
			p = emit_num(p, end, unum, 10, 0, 0);
			break;
		case 'x':
			unum = va_arg(ap, unsigned int);
			p = emit_num(p, end, unum, 16, 0, 0);
			break;
		case 'l':
			switch (*fmt++) {
			case 'd':
				snum = va_arg(ap, long);
				unum = snum < 0 ?
					       -(unsigned long long)snum :
					       (unsigned long long)snum;
				p = emit_num(p, end, unum, 10, 0,
					     snum < 0);
				break;
			case 'u':
				unum = va_arg(ap, unsigned long);
				p = emit_num(p, end, unum, 10, 0, 0);
				break;
			case 'x':
				unum = va_arg(ap, unsigned long);
				p = emit_num(p, end, unum, 16, 0, 0);
				break;
			default:
				p = emit(p, end, '%');
				p = emit(p, end, 'l');
				fmt--;
				break;
			}
			break;
		case 'p':
			unum = (unsigned long)va_arg(ap, void *);
			p = emit(p, end, '0');
			p = emit(p, end, 'x');
			p = emit_num(p, end, unum, 16, 0, 0);
			break;
		default:
			p = emit(p, end, '%');
			p = emit(p, end, fmt[-1]);
			break;
		}
	}

	if (!end)
		*p = '\0';
	else if (p <= end)
		*p = '\0';
	else
		*end = '\0';

	return (int)(p - buf);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	if (size == 0)
		return 0;

	return format(buf, buf + size - 1, fmt, ap);
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
	return format(buf, NULL, fmt, ap);
}

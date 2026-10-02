/* printf.c - formatted output: kprintf goes to the serial port and, until
 * the desktop takes over, the boot console. Supports %s %c %d %u %x %p
 * with an optional '-' or '0' flag and width. */
#include <stdarg.h>
#include "kernel.h"

int console_ready;

static void out_num(void (*out)(void *, char), void *arg, u32 v, int base,
		    int neg, int width, char pad, int left)
{
	char buf[12];
	int n = 0, len;

	do {
		buf[n++] = "0123456789abcdef"[v % base];
		v /= base;
	} while (v);
	if (neg)
		buf[n++] = '-';
	len = n;
	while (!left && width-- > len)
		out(arg, pad);
	while (n--)
		out(arg, buf[n]);
	while (left && width-- > len)
		out(arg, ' ');
}

void vformat(void (*out)(void *, char), void *arg, const char *fmt, va_list ap)
{
	for (; *fmt; fmt++) {
		int width = 0, left = 0;
		char pad = ' ';

		if (*fmt != '%') {
			out(arg, *fmt);
			continue;
		}
		fmt++;
		if (*fmt == '-')
			left = 1, fmt++;
		if (*fmt == '0')
			pad = '0', fmt++;
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + *fmt++ - '0';
		switch (*fmt) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			int len = strlen(s);

			while (!left && width-- > len)
				out(arg, ' ');
			while (*s)
				out(arg, *s++);
			while (left && width-- > len)
				out(arg, ' ');
			break;
		}
		case 'c':
			out(arg, (char)va_arg(ap, int));
			break;
		case 'd': {
			int v = va_arg(ap, int);

			out_num(out, arg, v < 0 ? -v : v, 10, v < 0, width, pad, left);
			break;
		}
		case 'u':
			out_num(out, arg, va_arg(ap, u32), 10, 0, width, pad, left);
			break;
		case 'x':
			out_num(out, arg, va_arg(ap, u32), 16, 0, width, pad, left);
			break;
		case 'p':
			out_num(out, arg, va_arg(ap, u32), 16, 0, 8, '0', 0);
			break;
		default:
			out(arg, *fmt);
		}
	}
}

static void kout(void *arg, char c)
{
	serial_putc(c);
	if (console_ready)
		con_putc(c);
}

void kprintf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vformat(kout, NULL, fmt, ap);
	va_end(ap);
}

struct sbuf {
	char *p, *end;
};

static void sout(void *arg, char c)
{
	struct sbuf *b = arg;

	if (b->p < b->end)
		*b->p++ = c;
}

int kvsnprintf(char *buf, int size, const char *fmt, va_list ap)
{
	struct sbuf b = { buf, buf + size - 1 };

	vformat(sout, &b, fmt, ap);
	*b.p = 0;
	return b.p - buf;
}

int ksnprintf(char *buf, int size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = kvsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}

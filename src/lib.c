/* lib.c - the few C library routines the compiler calls on its own. */
#include "kernel.h"

/* memcpy and memset are in fast.S */

int strlen(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	return p - s;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, unsigned long n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b)
			return (u8)*a - (u8)*b;
		if (!*a)
			break;
	}
	return 0;
}

char *strcpy(char *dst, const char *src)
{
	char *d = dst;

	while ((*d++ = *src++))
		;
	return dst;
}

void *memmove(void *dst, const void *src, unsigned long n)
{
	u8 *d = dst;
	const u8 *s = src;

	if (d <= s || d >= s + n)
		return memcpy(dst, src, n);
	while (n--)
		d[n] = s[n];
	return dst;
}

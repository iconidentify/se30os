/* The few <string.h> functions the omarchy68k C files use; se30os
 * provides them (src/fast.S, src/lib.c). */
#ifndef OMARCHY_COMPAT_STRING_H
#define OMARCHY_COMPAT_STRING_H
void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int c, unsigned long n);
int strcmp(const char *a, const char *b);
#endif

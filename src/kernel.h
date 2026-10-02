#ifndef KERNEL_H
#define KERNEL_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef short s16;
typedef int s32;

#define NULL ((void *)0)

/* Machine description, filled in from Penguin's bootinfo records. */
struct machine {
	u32 model;		/* Mac Gestalt ID; the SE/30 is 9 */
	u32 cpu, fpu, mmu;
	u32 mem_base, mem_size;	/* first memory chunk */
	u8 *video;		/* framebuffer, physical */
	u32 rowbytes, width, height, depth;
	volatile u8 *via1, *via2, *scc;
	u32 rombase;
	int nchunks;		/* every memory chunk Penguin found */
	struct { u32 addr, size; } chunk[8];
	char cmdline[256];	/* Penguin's command line */
};
int boot_option(const char *word);	/* is word on the command line? */
extern struct machine mach;

/* VIA (6522) registers, spaced 0x200 bytes apart on the Mac */
#define VIA_B	0x0000		/* port B data */
#define VIA_A	0x0200		/* port A data */
#define VIA_DDRB 0x0400
#define VIA_DDRA 0x0600
#define VIA_T1CL 0x0800
#define VIA_T1CH 0x0a00
#define VIA_T1LL 0x0c00
#define VIA_T1LH 0x0e00
#define VIA_T2CL 0x1000
#define VIA_T2CH 0x1200
#define VIA_SR	0x1400		/* shift register */
#define VIA_ACR	0x1600		/* auxiliary control */
#define VIA_PCR	0x1800		/* peripheral control */
#define VIA_IFR	0x1a00		/* interrupt flags */
#define VIA_IER	0x1c00		/* interrupt enable */
#define VIA_ANH	0x1e00		/* port A, no handshake */

#define VIA_INT_CA2	0x01
#define VIA_INT_CA1	0x02	/* VIA1: 60.15 Hz vertical blanking */
#define VIA_INT_SR	0x04	/* VIA1: ADB shift register */
#define VIA_INT_CB2	0x08
#define VIA_INT_CB1	0x10
#define VIA_INT_T2	0x20
#define VIA_INT_T1	0x40

static inline void irq_off(void)
{
	__asm__ volatile("move.w #0x2700,%%sr" ::: "memory");
}

static inline void irq_on(void)
{
	__asm__ volatile("move.w #0x2000,%%sr" ::: "memory");
}

/* Save SR and mask interrupts; restore with irq_restore(). */
static inline u16 irq_save(void)
{
	u16 sr;

	__asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) :: "memory");
	return sr;
}

static inline void irq_restore(u16 sr)
{
	__asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory");
}

/* Wait for the next interrupt. */
static inline void cpu_idle(void)
{
	__asm__ volatile("stop #0x2000" ::: "memory");
}

/* bootinfo.c */
void bootinfo_parse(const void *records);

/* serial.c: SCC channel A (modem port), polled */
void serial_init(void);
void serial_putc(char c);

/* screen.c: framebuffer and text console */
void screen_init(void);
void con_putc(char c);
void con_puts(const char *s);

/* printf.c: to serial and screen */
#include <stdarg.h>
void kprintf(const char *fmt, ...);
int ksnprintf(char *buf, int size, const char *fmt, ...);
int kvsnprintf(char *buf, int size, const char *fmt, va_list ap);
void vformat(void (*out)(void *, char), void *arg, const char *fmt, va_list ap);
extern int console_ready;

/* mmu.c */
void mmu_init(void);
void mmu_off(void);		/* for crash reports */
void cache_on(void);
void cache_off(void);

/* mem.c */
void mem_init(void);
void *kmalloc(u32 n);
void *kzalloc(u32 n);
void kfree(void *p);
u32 mem_free(void);

/* lib.c */
void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int c, unsigned long n);
void *memmove(void *dst, const void *src, unsigned long n);
int strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, unsigned long n);
char *strcpy(char *dst, const char *src);

/* interrupts: main.c */
extern volatile u32 ticks;

#endif

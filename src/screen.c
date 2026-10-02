/* screen.c - 1 bpp framebuffer and a 6x13 text console.
 * On the Mac a set bit is black; the console is black text on white. */
#include "kernel.h"
#include "input.h"

#define FONT_W 6
#define FONT_H 13

extern const unsigned char font6x13[256][FONT_H];

static int cols, rows, cx, cy;

void screen_init(void)
{
	u32 *p = (u32 *)mach.video;
	u32 n = mach.rowbytes * mach.height / 4;

	while (n--)
		*p++ = 0;		/* white */
	cols = mach.width / FONT_W;
	rows = mach.height / FONT_H;
	cx = cy = 0;
}

/* Draw one glyph at text cell (col, row). */
static void draw_glyph(int col, int row, unsigned char c)
{
	const unsigned char *g = font6x13[c];
	int x = col * FONT_W;
	int shift = x & 7;
	u8 *p = mach.video + row * FONT_H * mach.rowbytes + (x >> 3);
	u8 m0 = 0xfc >> shift;			/* glyph bits in the first byte */
	u8 m1 = (u8)(0xfc << (8 - shift));	/* and spilling into the next */
	int i;

	cursor_shield(x, row * FONT_H, x + FONT_W, (row + 1) * FONT_H);
	for (i = 0; i < FONT_H; i++, p += mach.rowbytes) {
		p[0] = (p[0] & ~m0) | (g[i] >> shift);
		if (shift > 2)
			p[1] = (p[1] & ~m1) | (u8)(g[i] << (8 - shift));
	}
	cursor_unshield();
}

static void scroll(void)
{
	u32 line = FONT_H * mach.rowbytes;	/* bytes per text line */
	u32 *dst = (u32 *)mach.video;
	u32 *src = (u32 *)(mach.video + line);
	u32 n = (rows - 1) * line / 4;

	cursor_shield(0, 0, mach.width, rows * FONT_H);
	while (n--)
		*dst++ = *src++;
	n = line / 4;
	while (n--)
		*dst++ = 0;
	cursor_unshield();
}

void con_putc(char c)
{
	switch (c) {
	case '\n':
		cx = 0;
		if (++cy == rows) {
			scroll();
			cy = rows - 1;
		}
		return;
	case '\r':
		cx = 0;
		return;
	case '\b':
		if (cx > 0)
			cx--;
		return;
	case '\t':
		do
			con_putc(' ');
		while (cx & 7);
		return;
	}
	draw_glyph(cx, cy, (unsigned char)c);
	if (++cx == cols)
		con_putc('\n');
}

void con_puts(const char *s)
{
	while (*s)
		con_putc(*s++);
}

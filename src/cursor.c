/* cursor.c - the mouse pointer.
 *
 * The pointer is redrawn from the ADB interrupt the moment a mouse packet
 * arrives, so it never waits for the main loop or the next vertical blank.
 * Code that draws to the screen brackets its drawing with cursor_shield()
 * and cursor_unshield(); the pointer is lifted only if it overlaps.
 */
#include "kernel.h"
#include "input.h"

#define CUR_H	16
#define HOT_X	1
#define HOT_Y	1

static const u16 arrow[CUR_H] = {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7c00, 0x7e00, 0x7f00,
	0x7f80, 0x7c00, 0x6c00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000,
};
static u16 arrow_mask[CUR_H];	/* the arrow grown by a pixel: its outline */

static int visible;		/* drawn on the screen now */
static int left, top;		/* where it is drawn */
static u32 save[CUR_H];		/* the screen underneath */
static int shielded;		/* cursor_shield() nesting depth */
static int sx0, sy0, sx1, sy1;	/* shielded area, in bytes and rows */

/* Row r of the pointer at (left, top): the 32-bit screen window it falls
 * in, and its mask and image bits within that window. */
static u32 *row(int r, u32 *m, u32 *d)
{
	int y = top + r, x = left & ~15;
	u32 mask = (u32)arrow_mask[r] << 16, data = (u32)arrow[r] << 16;

	if (y < 0 || y >= (int)mach.height)
		return NULL;
	if (left < 0) {
		x = 0;			/* off the left edge */
		mask <<= -left;
		data <<= -left;
	} else {
		mask >>= left & 15;
		data >>= left & 15;
		if (x + 32 > (int)mach.width) {
			/* off the right edge: keep out of the next row */
			u32 clip = ~0u << (x + 32 - mach.width);

			mask &= clip;
			data &= clip;
		}
	}
	*m = mask;
	*d = data;
	return (u32 *)(mach.video + y * mach.rowbytes + x / 8);
}

static void draw(void)
{
	int r;

	left = mouse_x - HOT_X;
	top = mouse_y - HOT_Y;
	for (r = 0; r < CUR_H; r++) {
		u32 m, d, *p = row(r, &m, &d);

		if (p) {
			save[r] = *p;
			*p = (*p & ~m) ^ d;
		}
	}
	visible = 1;
}

/* Put back what was under the pointer, only where it drew. */
static void erase(void)
{
	int r;

	for (r = 0; r < CUR_H; r++) {
		u32 m, d, *p = row(r, &m, &d);

		if (p)
			*p = (*p & ~m) | (save[r] & m);
	}
	visible = 0;
}

/* Does the pointer drawn at (l, t) touch any byte being shielded? */
static int overlaps(int l, int t)
{
	if (l < 0)
		l = 0;
	return shielded && t < sy1 && t + CUR_H > sy0 &&
	       (l >> 3) < sx1 && ((l + 15) >> 3) >= sx0;
}

void cursor_moved(void)
{
	if (visible)
		erase();
	if (!overlaps(mouse_x - HOT_X, mouse_y - HOT_Y))
		draw();			/* else cursor_unshield() will */
}

/* About to draw in pixels [x0,x1) x [y0,y1): keep the pointer out of it. */
void cursor_shield(int x0, int y0, int x1, int y1)
{
	u16 sr = irq_save();

	x0 >>= 3;
	x1 = (x1 + 7) >> 3;
	if (shielded++ == 0) {
		sx0 = x0, sy0 = y0, sx1 = x1, sy1 = y1;
	} else {
		if (x0 < sx0) sx0 = x0;
		if (y0 < sy0) sy0 = y0;
		if (x1 > sx1) sx1 = x1;
		if (y1 > sy1) sy1 = y1;
	}
	if (visible && overlaps(left, top))
		erase();
	irq_restore(sr);
}

void cursor_unshield(void)
{
	u16 sr = irq_save();

	if (--shielded == 0 && !visible)
		draw();
	irq_restore(sr);
}

void cursor_init(void)
{
	int r;

	for (r = 0; r < CUR_H; r++) {
		u16 m = 0;
		int i;

		for (i = r - 1; i <= r + 1; i++)
			if (i >= 0 && i < CUR_H)
				m |= arrow[i] | arrow[i] << 1 | arrow[i] >> 1;
		arrow_mask[r] = m;
	}
	mouse_x = mach.width / 2;
	mouse_y = mach.height / 2;
	draw();
}

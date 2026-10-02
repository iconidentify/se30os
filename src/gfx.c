/* gfx.c - 1 bit per pixel drawing: rectangles, the blitter and text.
 *
 * Everything works a long (32 pixels) at a time with edge masks; the
 * blitter shifts source longs into place when source and destination
 * don't share bit alignment.
 */
#include "kernel.h"
#include "gfx.h"

extern const unsigned char font6x13[256][13];

void blit_fwd(u32 *d, const u32 *s, int rows, int n, int sh, u32 lm, u32 rm,
	      long dstep, long sstep);

struct bitmap screen;
const struct font font_6x13 = { 6, 13, 10, font6x13 };

const u8 pat_black[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
const u8 pat_white[8];
const u8 pat_gray[8] = { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 };
const u8 pat_ltgray[8] = { 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22 };
const u8 pat_dkgray[8] = { 0x77, 0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77, 0xdd };

int rect_sect(const struct rect *a, const struct rect *b, struct rect *out)
{
	struct rect r;

	r.left = a->left > b->left ? a->left : b->left;
	r.top = a->top > b->top ? a->top : b->top;
	r.right = a->right < b->right ? a->right : b->right;
	r.bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
	*out = r;
	return !rect_empty(&r);
}

void rect_union(struct rect *a, const struct rect *b)
{
	if (rect_empty(b))
		return;
	if (rect_empty(a)) {
		*a = *b;
		return;
	}
	if (b->left < a->left) a->left = b->left;
	if (b->top < a->top) a->top = b->top;
	if (b->right > a->right) a->right = b->right;
	if (b->bottom > a->bottom) a->bottom = b->bottom;
}

void rect_offset(struct rect *r, int dx, int dy)
{
	r->left += dx;
	r->right += dx;
	r->top += dy;
	r->bottom += dy;
}

void rect_inset(struct rect *r, int dx, int dy)
{
	r->left += dx;
	r->right -= dx;
	r->top += dy;
	r->bottom -= dy;
}

void gfx_init(void)
{
	screen.base = mach.video;
	screen.rowbytes = mach.rowbytes;
	screen.width = mach.width;
	screen.height = mach.height;
}

int bitmap_alloc(struct bitmap *bm, int width, int height)
{
	u32 rb = ((width + 31) >> 5) * 4;
	u8 *p = kzalloc(rb * height + 8);

	if (!p)
		return 0;
	bm->base = p + 4;
	bm->rowbytes = rb;
	bm->width = width;
	bm->height = height;
	return 1;
}

void bitmap_free(struct bitmap *bm)
{
	if (bm->base)
		kfree(bm->base - 4);
	bm->base = NULL;
}

/* Clip r to the bitmap and to clip; 0 if nothing is left. */
static int clip_to(const struct bitmap *bm, const struct rect *r,
		   const struct rect *clip, struct rect *out)
{
	struct rect b = { 0, 0, bm->width, bm->height };

	if (!rect_sect(r, &b, out))
		return 0;
	return !clip || rect_sect(out, clip, out);
}

static inline u32 *row_addr(const struct bitmap *bm, int x, int y)
{
	return (u32 *)(bm->base + y * bm->rowbytes) + (x >> 5);
}

static inline u32 combine(u32 d, u32 s, u32 m, int mode)
{
	switch (mode) {
	case M_COPY:	return (d & ~m) | (s & m);
	case M_OR:	return d | (s & m);
	case M_XOR:	return d ^ (s & m);
	default:	return d & ~(s & m);
	}
}

void fill_rect(const struct bitmap *bm, const struct rect *rr, const u8 *pat,
	       int mode, const struct rect *clip)
{
	struct rect r;
	u32 *row, lm, rm;
	int n, y;

	if (!clip_to(bm, rr, clip, &r))
		return;
	row = row_addr(bm, r.left, r.top);
	n = ((r.right - 1) >> 5) - (r.left >> 5);	/* longs after the first */
	lm = ~0u >> (r.left & 31);
	rm = ~0u << (31 - ((r.right - 1) & 31));
	if (!n)
		lm &= rm;
	for (y = r.top; y < r.bottom; y++) {
		u32 p = pat[y & 7] * 0x01010101u, *w = row;
		int i;

		*w = combine(*w, p, lm, mode);
		if (n) {
			w++;
			switch (mode) {
			case M_COPY:
				for (i = n - 1; i > 0; i--)
					*w++ = p;
				break;
			case M_OR:
				for (i = n - 1; i > 0; i--)
					*w++ |= p;
				break;
			case M_XOR:
				for (i = n - 1; i > 0; i--)
					*w++ ^= p;
				break;
			default:
				for (i = n - 1; i > 0; i--)
					*w++ &= ~p;
			}
			*w = combine(*w, p, rm, mode);
		}
		row = (u32 *)((u8 *)row + bm->rowbytes);
	}
}

void frame_rect(const struct bitmap *bm, const struct rect *r, int mode,
		const struct rect *clip)
{
	struct rect e;

	e = mkrect(r->left, r->top, r->right, r->top + 1);
	fill_rect(bm, &e, pat_black, mode, clip);
	e = mkrect(r->left, r->bottom - 1, r->right, r->bottom);
	fill_rect(bm, &e, pat_black, mode, clip);
	e = mkrect(r->left, r->top + 1, r->left + 1, r->bottom - 1);
	fill_rect(bm, &e, pat_black, mode, clip);
	e = mkrect(r->right - 1, r->top + 1, r->right, r->bottom - 1);
	fill_rect(bm, &e, pat_black, mode, clip);
}

/* One row of the blitter: n+1 destination longs starting at d, the first
 * and last masked by lm and rm. Source bits come from s[0], s[1], ...
 * shifted left by sh (0-31). dir < 0 walks right to left. */
static void blit_row(u32 *d, const u32 *s, int n, int sh, u32 lm, u32 rm,
		     int mode, int dir)
{
	int i;

	if (dir > 0) {
		u32 v = sh ? (s[0] << sh) | (s[1] >> (32 - sh)) : s[0];

		*d = combine(*d, v, lm, mode);
		if (!n)
			return;
		d++, s++;
		if (sh == 0 && mode == M_COPY) {
			for (i = n - 1; i > 0; i--)
				*d++ = *s++;
		} else if (mode == M_COPY) {
			u32 a = s[0];

			for (i = n - 1; i > 0; i--) {
				u32 b = s[1];

				*d++ = (a << sh) | (b >> (32 - sh));
				a = b;
				s++;
			}
		} else {
			for (i = n - 1; i > 0; i--, d++, s++) {
				v = sh ? (s[0] << sh) | (s[1] >> (32 - sh)) : s[0];
				*d = combine(*d, v, ~0u, mode);
			}
		}
		v = sh ? (s[0] << sh) | (s[1] >> (32 - sh)) : s[0];
		*d = combine(*d, v, rm, mode);
	} else {
		/* start at the rightmost long */
		u32 v;

		d += n;
		s += n;
		v = sh ? (s[0] << sh) | (s[1] >> (32 - sh)) : s[0];
		*d = combine(*d, v, n ? rm : lm, mode);
		if (!n)
			return;
		for (i = n - 1; i > 0; i--) {
			d--, s--;
			v = sh ? (s[0] << sh) | (s[1] >> (32 - sh)) : s[0];
			*d = combine(*d, v, ~0u, mode);
		}
		d--, s--;
		v = sh ? (s[0] << sh) | (s[1] >> (32 - sh)) : s[0];
		*d = combine(*d, v, lm, mode);
	}
}

void copy_bits(const struct bitmap *src, const struct rect *sr,
	       const struct bitmap *dst, int dx, int dy, int mode,
	       const struct rect *clip)
{
	struct rect d, s;
	const u8 *srow;
	u8 *drow;
	int n, sh, sx, h, dir = 1;
	u32 lm, rm;
	long sstep, dstep;

	/* clip the destination, then the source, then the destination
	   again to whatever source survived */
	d = *sr;
	rect_offset(&d, dx - sr->left, dy - sr->top);
	if (!clip_to(dst, &d, clip, &d))
		return;
	s = d;
	rect_offset(&s, sr->left - dx, sr->top - dy);
	if (!clip_to(src, &s, NULL, &s))
		return;
	d = s;
	rect_offset(&d, dx - sr->left, dy - sr->top);

	/* source bit x maps to destination bit x + (d.left - s.left) */
	n = ((d.right - 1) >> 5) - (d.left >> 5);
	lm = ~0u >> (d.left & 31);
	rm = ~0u << (31 - ((d.right - 1) & 31));
	if (!n)
		lm &= rm;
	/* first source bit that lands in the first destination long */
	sx = s.left - (d.left & 31);
	sh = sx & 31;
	h = d.bottom - d.top;

	if (src == dst || src->base == dst->base) {
		if (d.top > s.top) {
			/* overlapping, moving down: bottom row first */
			srow = src->base + (s.bottom - 1) * src->rowbytes;
			drow = dst->base + (d.bottom - 1) * dst->rowbytes;
			sstep = -(long)src->rowbytes;
			dstep = -(long)dst->rowbytes;
			goto go;
		}
		if (d.top == s.top && d.left > s.left)
			dir = -1;	/* same rows, moving right */
	}
	srow = src->base + s.top * src->rowbytes;
	drow = dst->base + d.top * dst->rowbytes;
	sstep = src->rowbytes;
	dstep = dst->rowbytes;
go:
	if (mode == M_COPY && dir > 0) {
		/* A shift by whole bytes needs no shifting at all: the
		   68030 reads longs from any byte address. */
		if ((sh & 7) == 0)
			blit_fwd((u32 *)drow + (d.left >> 5),
				 (const u32 *)(srow + (sx >> 3)), h, n, 0, lm, rm,
				 dstep, sstep);
		else
			blit_fwd((u32 *)drow + (d.left >> 5),
				 (const u32 *)srow + (sx >> 5), h, n, sh, lm, rm,
				 dstep, sstep);
		return;
	}
	while (h--) {
		/* sx may be negative (bits before the row): floor divide */
		const u32 *sp = (const u32 *)srow + (sx >> 5);

		blit_row((u32 *)drow + (d.left >> 5), sp, n, sh, lm, rm, mode,
			 dir);
		srow += sstep;
		drow += dstep;
	}
}

int draw_char(const struct bitmap *bm, int x, int y, u8 c,
	      const struct font *f, int mode, const struct rect *clip)
{
	struct rect r = { x, y, x + f->width, y + f->height }, cr;
	const u8 *g = f->glyphs[c];
	int i;

	if (!clip_to(bm, &r, clip, &cr))
		return f->width;
	for (i = cr.top - y; i < cr.bottom - y; i++) {
		u32 *w = row_addr(bm, x, y + i);
		int sh = x & 31;
		u32 bits = (u32)g[i] << 24, mask = (u32)(0xff << (8 - f->width)) << 24;
		u32 cm = (~0u >> (cr.left - (x & ~31) > 31 ? 31 : 0));

		(void)cm;
		/* clip horizontally by masking columns outside cr */
		if (cr.left > x)
			mask &= ~0u >> (cr.left - x);
		if (cr.right < x + f->width)
			mask &= ~(~0u >> (cr.right - x));
		if (mode == M_OR && !g[i])
			continue;
		w[0] = combine(w[0], bits >> sh, mask >> sh, mode);
		if (sh + f->width > 32)
			w[1] = combine(w[1], bits << (32 - sh), mask << (32 - sh), mode);
	}
	return f->width;
}

int draw_text(const struct bitmap *bm, int x, int y, const char *s,
	      const struct font *f, int mode, const struct rect *clip)
{
	int x0 = x;

	while (*s)
		x += draw_char(bm, x, y, (u8)*s++, f, mode, clip);
	return x - x0;
}

int text_width(const char *s, const struct font *f)
{
	return strlen(s) * f->width;
}

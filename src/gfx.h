#ifndef GFX_H
#define GFX_H

/* Rectangles are half-open: [left, right) x [top, bottom). */
struct rect {
	s16 left, top, right, bottom;
};

/* A 1 bit per pixel image; set bits are black. Rows are whole longs and
 * the most significant bit of each is the leftmost pixel. Offscreen
 * bitmaps have a spare long before and after the pixels, since the
 * blitter reads one long beyond a row on either side. */
struct bitmap {
	u8 *base;
	u32 rowbytes;
	s16 width, height;
};

struct font {
	u8 width, height, ascent;
	const u8 (*glyphs)[13];		/* height bytes per glyph, MSB left */
};

enum { M_COPY, M_OR, M_XOR, M_BIC };

extern struct bitmap screen;
extern const struct font font_6x13;
extern const u8 pat_black[8], pat_white[8], pat_gray[8], pat_ltgray[8],
	pat_dkgray[8];

static inline struct rect mkrect(int l, int t, int r, int b)
{
	struct rect x = { l, t, r, b };
	return x;
}

static inline int rect_empty(const struct rect *r)
{
	return r->left >= r->right || r->top >= r->bottom;
}

static inline int rect_has(const struct rect *r, int x, int y)
{
	return x >= r->left && x < r->right && y >= r->top && y < r->bottom;
}

int rect_sect(const struct rect *a, const struct rect *b, struct rect *out);
void rect_union(struct rect *a, const struct rect *b);
void rect_offset(struct rect *r, int dx, int dy);
void rect_inset(struct rect *r, int dx, int dy);

void gfx_init(void);
int bitmap_alloc(struct bitmap *bm, int width, int height);
void bitmap_free(struct bitmap *bm);

/* All drawing is clipped to the bitmap and to clip (NULL: no extra clip). */
void fill_rect(const struct bitmap *bm, const struct rect *r, const u8 *pat,
	       int mode, const struct rect *clip);
void frame_rect(const struct bitmap *bm, const struct rect *r, int mode,
		const struct rect *clip);
void copy_bits(const struct bitmap *src, const struct rect *sr,
	       const struct bitmap *dst, int dx, int dy, int mode,
	       const struct rect *clip);
int draw_char(const struct bitmap *bm, int x, int y, u8 c,
	      const struct font *f, int mode, const struct rect *clip);
int draw_text(const struct bitmap *bm, int x, int y, const char *s,
	      const struct font *f, int mode, const struct rect *clip);
int text_width(const char *s, const struct font *f);

#endif

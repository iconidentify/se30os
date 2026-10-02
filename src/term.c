/* term.c - a terminal window that speaks enough xterm for modern
 * programs: UTF-8 with the 4,000 glyphs of the 6x13 font, colours turned
 * into black and white, scrolling regions, insert and delete, the
 * alternate screen and DEC line drawing.
 *
 * Output only changes the character grid. The window's bitmap is redrawn
 * from the grid just before it goes to the screen, sixteen characters at
 * a time, and any scrolling since the last time becomes one blit, so a
 * burst of output costs about a screenful of drawing per frame however
 * many lines go past.
 */
#include "kernel.h"
#include "term.h"

#define CW 6
#define CH 13

void text_rows(u32 *dst, const u16 *glyphs, int groups, const u8 *ftab,
	       long stride, long rowbytes, int height);

extern const int font_uni_count;
extern const u16 font_uni_cp[];
extern const u8 font_uni_bits[][13];

static u16 *glyph_of;		/* code point -> glyph number */
static u8 *ftab;		/* CH tables of nglyph 6-bit glyph rows */
static int nglyph;
static u16 g_space, g_repl, g_wide;

static void fonts_init(void)
{
	int i, r;

	nglyph = font_uni_count;
	glyph_of = kmalloc(0x10000 * sizeof(u16));
	ftab = kmalloc(CH * nglyph);
	for (i = 0; i < nglyph; i++)
		for (r = 0; r < CH; r++)
			ftab[r * nglyph + i] = font_uni_bits[i][r] >> 2;
	g_repl = 0;
	for (i = 0; i < nglyph; i++)
		if (font_uni_cp[i] == 0xfffd)
			g_repl = i;
	for (i = 0; i < 0x10000; i++)
		glyph_of[i] = g_repl;
	for (i = 0; i < nglyph; i++)
		glyph_of[font_uni_cp[i]] = i;
	g_space = glyph_of[' '];
	g_wide = glyph_of[0x25a1];	/* white square: CJK and emoji */
}

/* ---- character widths ---- */

struct range {
	u32 lo, hi;
};

static const struct range zero_width[] = {
	{ 0x0300, 0x036f }, { 0x0483, 0x0489 }, { 0x0591, 0x05bd },
	{ 0x0610, 0x061a }, { 0x064b, 0x065f }, { 0x0e31, 0x0e31 },
	{ 0x0e34, 0x0e3a }, { 0x1ab0, 0x1aff }, { 0x1dc0, 0x1dff },
	{ 0x200b, 0x200f }, { 0x202a, 0x202e }, { 0x2060, 0x2064 },
	{ 0x20d0, 0x20ff }, { 0xfe00, 0xfe0f }, { 0xfe20, 0xfe2f },
	{ 0xfeff, 0xfeff }, { 0xe0100, 0xe01ef },
};

static const struct range double_width[] = {
	{ 0x1100, 0x115f }, { 0x231a, 0x231b }, { 0x2329, 0x232a },
	{ 0x23e9, 0x23ec }, { 0x23f0, 0x23f0 }, { 0x23f3, 0x23f3 },
	{ 0x25fd, 0x25fe }, { 0x2614, 0x2615 }, { 0x2648, 0x2653 },
	{ 0x267f, 0x267f }, { 0x2693, 0x2693 }, { 0x26a1, 0x26a1 },
	{ 0x26aa, 0x26ab }, { 0x26bd, 0x26be }, { 0x26c4, 0x26c5 },
	{ 0x26ce, 0x26ce }, { 0x26d4, 0x26d4 }, { 0x26ea, 0x26ea },
	{ 0x26f2, 0x26f3 }, { 0x26f5, 0x26f5 }, { 0x26fa, 0x26fa },
	{ 0x26fd, 0x26fd }, { 0x2705, 0x2705 }, { 0x270a, 0x270b },
	{ 0x2728, 0x2728 }, { 0x274c, 0x274c }, { 0x274e, 0x274e },
	{ 0x2753, 0x2755 }, { 0x2757, 0x2757 }, { 0x2795, 0x2797 },
	{ 0x27b0, 0x27b0 }, { 0x27bf, 0x27bf }, { 0x2b1b, 0x2b1c },
	{ 0x2b50, 0x2b50 }, { 0x2b55, 0x2b55 }, { 0x2e80, 0x303e },
	{ 0x3041, 0x33ff }, { 0x3400, 0x4dbf }, { 0x4e00, 0x9fff },
	{ 0xa000, 0xa4cf }, { 0xa960, 0xa97f }, { 0xac00, 0xd7a3 },
	{ 0xf900, 0xfaff }, { 0xfe10, 0xfe19 }, { 0xfe30, 0xfe6f },
	{ 0xff00, 0xff60 }, { 0xffe0, 0xffe6 }, { 0x1f004, 0x1f004 },
	{ 0x1f0cf, 0x1f0cf }, { 0x1f18e, 0x1f18e }, { 0x1f191, 0x1f19a },
	{ 0x1f200, 0x1f251 }, { 0x1f300, 0x1f64f }, { 0x1f680, 0x1f6ff },
	{ 0x1f7e0, 0x1f7eb }, { 0x1f90c, 0x1f9ff }, { 0x1fa70, 0x1faff },
	{ 0x20000, 0x2fffd }, { 0x30000, 0x3fffd },
};

static int in_ranges(u32 c, const struct range *r, int n)
{
	int lo = 0, hi = n - 1;

	while (lo <= hi) {
		int mid = (lo + hi) / 2;

		if (c < r[mid].lo)
			hi = mid - 1;
		else if (c > r[mid].hi)
			lo = mid + 1;
		else
			return 1;
	}
	return 0;
}

#define NELEM(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int char_width(u32 c)
{
	if (c < 0x300)
		return 1;
	if (in_ranges(c, zero_width, NELEM(zero_width)))
		return 0;
	if (in_ranges(c, double_width, NELEM(double_width)))
		return 2;
	return 1;
}

/* DEC special graphics for 0x5f-0x7e: the VT100's line drawing set */
static const u16 dec_graphics[32] = {
	0x00a0, 0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0,
	0x00b1, 0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c,
	0x23ba, 0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534,
	0x252c, 0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7,
};

/* ---- colours: a background darker than nearly white shows inverted ---- */

static int luma(int r, int g, int b)
{
	return (r * 54 + g * 183 + b * 19) >> 8;
}

static int palette_luma(int n)
{
	static const u8 base[16][3] = {
		{ 0, 0, 0 }, { 205, 0, 0 }, { 0, 205, 0 }, { 205, 205, 0 },
		{ 0, 0, 238 }, { 205, 0, 205 }, { 0, 205, 205 }, { 229, 229, 229 },
		{ 127, 127, 127 }, { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 },
		{ 92, 92, 255 }, { 255, 0, 255 }, { 0, 255, 255 }, { 255, 255, 255 },
	};
	static const u8 level[6] = { 0, 95, 135, 175, 215, 255 };

	if (n < 0 || n > 255)
		return 255;
	if (n < 16)
		return luma(base[n][0], base[n][1], base[n][2]);
	if (n < 232) {
		n -= 16;
		return luma(level[n / 36], level[n / 6 % 6], level[n % 6]);
	}
	return 8 + 10 * (n - 232);
}

#define DARK(l) ((l) < 240)

static u8 cur_attr(struct term *t)
{
	return t->sgr | ((t->reverse ^ t->bg_dark) ? A_INV : 0);
}

static u8 erase_attr(struct term *t)
{
	return t->bg_dark ? A_INV : 0;	/* erasing paints the background */
}

/* ---- the grid ---- */

static void touch(struct term *t, int row, int c0, int c1)
{
	if (c0 >= c1)
		return;
	if (t->hi[row] == 0) {
		t->lo[row] = c0;
		t->hi[row] = c1;
		return;
	}
	if (c0 < t->lo[row])
		t->lo[row] = c0;
	if (c1 > t->hi[row])
		t->hi[row] = c1;
}

static void clear_cells(struct term *t, int row, int c0, int c1, u8 a)
{
	u16 *g = t->glyph[row];
	u8 *at = t->attr[row];
	int i;

	if (c0 < 0)
		c0 = 0;
	if (c1 > t->cols)
		c1 = t->cols;
	for (i = c0; i < c1; i++) {
		g[i] = g_space;
		at[i] = a;
	}
	touch(t, row, c0, c1);
}

static void xor_cursor(struct term *t, int cx, int cy)
{
	struct rect r = mkrect(t->x0 + cx * CW, t->y0 + cy * CH,
			       t->x0 + (cx + 1) * CW, t->y0 + (cy + 1) * CH);

	fill_rect(&t->w->bm, &r, pat_black, M_XOR, NULL);
	win_invalidate(t->w, &r);
}

static void hide_cursor(struct term *t)
{
	if (t->ccy >= 0) {
		xor_cursor(t, t->ccx, t->ccy);
		t->ccy = -1;
	}
}

static void inval_rows(struct term *t, int r0, int r1)
{
	struct rect r = mkrect(t->x0, t->y0 + r0 * CH, t->x0 + t->cols * CW,
			       t->y0 + (r1 + 1) * CH);

	win_invalidate(t->w, &r);
}

/* Do the bitmap's share of the scrolling done to the grid so far. */
static void apply_scroll(struct term *t)
{
	int n = t->sc_n, top = t->sc_top, bot = t->sc_bot;
	struct rect r;

	if (!n)
		return;
	hide_cursor(t);
	t->sc_n = 0;
	if (n > 0) {
		r = mkrect(t->x0, t->y0 + (top + n) * CH, t->x0 + t->cols * CW,
			   t->y0 + (bot + 1) * CH);
		copy_bits(&t->w->bm, &r, &t->w->bm, t->x0, t->y0 + top * CH,
			  M_COPY, NULL);
	} else {
		r = mkrect(t->x0, t->y0 + top * CH, t->x0 + t->cols * CW,
			   t->y0 + (bot + 1 + n) * CH);
		copy_bits(&t->w->bm, &r, &t->w->bm, t->x0, t->y0 + (top - n) * CH,
			  M_COPY, NULL);
	}
	inval_rows(t, top, bot);
}

/* Scroll rows top..bot up by n (down if n < 0), blanking what comes in. */
static void scroll(struct term *t, int top, int bot, int n)
{
	int h = bot - top + 1, i, up = n > 0, k = up ? n : -n;
	u16 *sg[TERM_MAXROWS];
	u8 *sa[TERM_MAXROWS];
	u8 ea = erase_attr(t);

	if (!n || h <= 0)
		return;
	if (k >= h) {
		for (i = top; i <= bot; i++)
			clear_cells(t, i, 0, t->cols, ea);
		return;
	}
	/* the bitmap follows lazily, one region and direction at a time */
	if (t->sc_n && (t->sc_top != top || t->sc_bot != bot ||
			(t->sc_n > 0) != up))
		apply_scroll(t);
	if (!t->sc_n) {
		t->sc_top = top;
		t->sc_bot = bot;
	}
	t->sc_n += n;
	if (t->sc_n >= h || -t->sc_n >= h) {
		/* scrolled right through: redraw it all instead */
		hide_cursor(t);
		t->sc_n = 0;
		for (i = top; i <= bot; i++)
			touch(t, i, 0, t->cols);
	}

	/* rotate the rows */
	for (i = 0; i < h; i++) {
		sg[i] = t->glyph[top + i];
		sa[i] = t->attr[top + i];
	}
	for (i = 0; i < h; i++) {
		int from = up ? (i + k) % h : (i - k + h) % h;

		t->glyph[top + i] = sg[from];
		t->attr[top + i] = sa[from];
	}
	if (up) {
		for (i = top; i <= bot - k; i++) {
			t->lo[i] = t->lo[i + k];
			t->hi[i] = t->hi[i + k];
		}
		for (i = bot - k + 1; i <= bot; i++) {
			t->hi[i] = 0;
			clear_cells(t, i, 0, t->cols, ea);
		}
	} else {
		for (i = bot; i >= top + k; i--) {
			t->lo[i] = t->lo[i - k];
			t->hi[i] = t->hi[i - k];
		}
		for (i = top; i < top + k; i++) {
			t->hi[i] = 0;
			clear_cells(t, i, 0, t->cols, ea);
		}
	}
}

static void linefeed(struct term *t)
{
	if (t->cy == t->bot)
		scroll(t, t->top, t->bot, 1);
	else if (t->cy < t->rows - 1)
		t->cy++;
}

static void reverse_index(struct term *t)
{
	if (t->cy == t->top)
		scroll(t, t->top, t->bot, -1);
	else if (t->cy > 0)
		t->cy--;
}

static void insert_cells(struct term *t, int n)
{
	u16 *g = t->glyph[t->cy];
	u8 *a = t->attr[t->cy];
	int i;

	if (n > t->cols - t->cx)
		n = t->cols - t->cx;
	for (i = t->cols - 1; i >= t->cx + n; i--) {
		g[i] = g[i - n];
		a[i] = a[i - n];
	}
	clear_cells(t, t->cy, t->cx, t->cx + n, erase_attr(t));
	touch(t, t->cy, t->cx, t->cols);
}

static void delete_cells(struct term *t, int n)
{
	u16 *g = t->glyph[t->cy];
	u8 *a = t->attr[t->cy];
	int i;

	if (n > t->cols - t->cx)
		n = t->cols - t->cx;
	for (i = t->cx; i < t->cols - n; i++) {
		g[i] = g[i + n];
		a[i] = a[i + n];
	}
	clear_cells(t, t->cy, t->cols - n, t->cols, erase_attr(t));
	touch(t, t->cy, t->cx, t->cols);
}

static void put_char(struct term *t, u32 c)
{
	int w = char_width(c);
	u16 g;
	u8 a = cur_attr(t);

	if (w == 0)
		return;			/* combining marks: not drawn */
	if (c >= 0x5f && c <= 0x7e && t->charset[t->gl])
		c = dec_graphics[c - 0x5f];
	if (t->wrap_pending) {
		t->wrap_pending = 0;
		if (t->autowrap) {
			t->cx = 0;
			linefeed(t);
		}
	}
	if (w == 2 && t->cx == t->cols - 1) {
		if (!t->autowrap)
			return;
		clear_cells(t, t->cy, t->cx, t->cols, erase_attr(t));
		t->cx = 0;
		linefeed(t);
	}
	if (t->insert)
		insert_cells(t, w);
	g = w == 2 ? g_wide : (c < 0x10000 ? glyph_of[c] : g_repl);
	t->glyph[t->cy][t->cx] = g;
	t->attr[t->cy][t->cx] = a;
	touch(t, t->cy, t->cx, t->cx + w);
	if (w == 2) {
		t->glyph[t->cy][t->cx + 1] = g_space;
		t->attr[t->cy][t->cx + 1] = a | A_WIDE_R;
	}
	t->cx += w;
	if (t->cx >= t->cols) {
		t->cx = t->cols - 1;
		t->wrap_pending = 1;
	}
}

/* ---- escape sequences ---- */

enum { S_GROUND, S_ESC, S_ESC_INTER, S_CSI, S_OSC, S_OSC_ESC, S_DCS,
       S_DCS_ESC };

static void reply(struct term *t, const char *s)
{
	if (t->reply)
		t->reply(t, s, strlen(s));
}

static int P(struct term *t, int i, int def)
{
	return i < t->nparam && t->param[i] > 0 ? t->param[i] : def;
}

static void clamp_cursor(struct term *t)
{
	int top = t->origin ? t->top : 0, bot = t->origin ? t->bot : t->rows - 1;

	if (t->cx < 0) t->cx = 0;
	if (t->cx >= t->cols) t->cx = t->cols - 1;
	if (t->cy < top) t->cy = top;
	if (t->cy > bot) t->cy = bot;
}

static void save_cursor(struct term *t)
{
	t->saved.cx = t->cx;
	t->saved.cy = t->cy;
	t->saved.wrap_pending = t->wrap_pending;
	t->saved.origin = t->origin;
	t->saved.sgr = t->sgr;
	t->saved.reverse = t->reverse;
	t->saved.bg_dark = t->bg_dark;
	t->saved.charset[0] = t->charset[0];
	t->saved.charset[1] = t->charset[1];
	t->saved.gl = t->gl;
}

static void restore_cursor(struct term *t)
{
	t->cx = t->saved.cx;
	t->cy = t->saved.cy;
	t->wrap_pending = t->saved.wrap_pending;
	t->origin = t->saved.origin;
	t->sgr = t->saved.sgr;
	t->reverse = t->saved.reverse;
	t->bg_dark = t->saved.bg_dark;
	t->charset[0] = t->saved.charset[0];
	t->charset[1] = t->saved.charset[1];
	t->gl = t->saved.gl;
	clamp_cursor(t);
}

static void mark_all(struct term *t)
{
	int i;

	for (i = 0; i < t->rows; i++)
		touch(t, i, 0, t->cols);
}

static void set_alt(struct term *t, int on, int clear)
{
	int i;

	if (on == t->alt)
		return;
	hide_cursor(t);
	t->sc_n = 0;			/* everything is redrawn anyway */
	for (i = 0; i < t->rows; i++) {
		u16 *g = t->glyph[i];
		u8 *a = t->attr[i];

		t->glyph[i] = t->alt_glyph[i];
		t->attr[i] = t->alt_attr[i];
		t->alt_glyph[i] = g;
		t->alt_attr[i] = a;
	}
	t->alt = on;
	if (clear)
		for (i = 0; i < t->rows; i++)
			clear_cells(t, i, 0, t->cols, 0);
	mark_all(t);
}

static void erase_display(struct term *t, int mode)
{
	u8 ea = erase_attr(t);
	int i;

	if (mode == 0) {
		clear_cells(t, t->cy, t->cx, t->cols, ea);
		for (i = t->cy + 1; i < t->rows; i++)
			clear_cells(t, i, 0, t->cols, ea);
	} else if (mode == 1) {
		for (i = 0; i < t->cy; i++)
			clear_cells(t, i, 0, t->cols, ea);
		clear_cells(t, t->cy, 0, t->cx + 1, ea);
	} else if (mode == 2 || mode == 3) {
		for (i = 0; i < t->rows; i++)
			clear_cells(t, i, 0, t->cols, ea);
	}
}

static void sgr(struct term *t)
{
	int i;

	if (!t->nparam) {
		t->sgr = t->reverse = t->bg_dark = 0;
		return;
	}
	for (i = 0; i < t->nparam; i++) {
		int p = t->param[i] < 0 ? 0 : t->param[i];

		switch (p) {
		case 0: t->sgr = t->reverse = t->bg_dark = 0; break;
		case 1: t->sgr |= A_BOLD; break;
		case 4: case 21: t->sgr |= A_UNDER; break;
		case 7: t->reverse = 1; break;
		case 9: t->sgr |= A_STRIKE; break;
		case 22: t->sgr &= ~A_BOLD; break;
		case 24: t->sgr &= ~A_UNDER; break;
		case 27: t->reverse = 0; break;
		case 29: t->sgr &= ~A_STRIKE; break;
		case 49: t->bg_dark = 0; break;
		case 38:
		case 48: {
			/* 38;5;n  38;2;r;g;b  (and the 38:2::r:g:b form) */
			int kind = i + 1 < t->nparam ? t->param[i + 1] : 0, l = 255;

			if (kind == 5 && i + 2 < t->nparam) {
				l = palette_luma(t->param[i + 2]);
				i += 2;
			} else if (kind == 2) {
				int j = i + 2, left = t->nparam - j;

				if (left >= 4)
					j++;	/* colour space id */
				if (t->nparam - j >= 3)
					l = luma(t->param[j], t->param[j + 1],
						 t->param[j + 2]);
				i = j + 2;
			} else {
				i++;
			}
			if (p == 48)
				t->bg_dark = DARK(l);
			break;
		}
		default:
			if (p >= 40 && p <= 47)
				t->bg_dark = DARK(palette_luma(p - 40));
			else if (p >= 100 && p <= 107)
				t->bg_dark = DARK(palette_luma(p - 100 + 8));
		}
	}
}

static void set_mode(struct term *t, int on)
{
	int i;

	for (i = 0; i < t->nparam; i++) {
		int p = t->param[i];

		if (t->priv != '?') {
			if (p == 4)
				t->insert = on;
			continue;
		}
		switch (p) {
		case 1: t->app_cursor = on; break;
		case 6:
			t->origin = on;
			t->cx = 0;
			t->cy = on ? t->top : 0;
			break;
		case 7: t->autowrap = on; break;
		case 25: t->cursor_hidden = !on; break;
		case 47:
		case 1047:
			set_alt(t, on, on);
			break;
		case 1049:
			if (on) {
				save_cursor(t);
				set_alt(t, 1, 1);
			} else {
				set_alt(t, 0, 0);
				restore_cursor(t);
			}
			break;
		case 1000: case 1002: case 1003: case 1006:
			t->mouse_mode = on ? p : 0;
			break;
		case 2004: t->bracketed_paste = on; break;
		}
	}
}

static void csi(struct term *t, char c)
{
	char buf[32];
	int n = P(t, 0, 1), i;

	if (t->inter) {
		if (t->inter == '!' && c == 'p')
			term_reset(t);		/* DECSTR, close enough */
		return;			/* cursor style and the like */
	}
	switch (c) {
	case '@': insert_cells(t, n); break;
	case 'A': t->cy -= n; if (t->cy < t->top && t->cy + n >= t->top) t->cy = t->top; break;
	case 'B': case 'e':
		t->cy += n;
		if (t->cy > t->bot && t->cy - n <= t->bot)
			t->cy = t->bot;
		break;
	case 'C': case 'a': t->cx += n; break;
	case 'D': t->cx -= n; break;
	case 'E': t->cy += n; t->cx = 0; break;
	case 'F': t->cy -= n; t->cx = 0; break;
	case 'G': case '`': t->cx = n - 1; break;
	case 'H': case 'f':
		t->cy = P(t, 0, 1) - 1 + (t->origin ? t->top : 0);
		t->cx = P(t, 1, 1) - 1;
		break;
	case 'I':
		while (n--) {
			do
				t->cx++;
			while (t->cx < t->cols - 1 && !t->tabs[t->cx]);
		}
		break;
	case 'J': erase_display(t, P(t, 0, 0)); break;
	case 'K':
		i = P(t, 0, 0);
		if (i == 0)
			clear_cells(t, t->cy, t->cx, t->cols, erase_attr(t));
		else if (i == 1)
			clear_cells(t, t->cy, 0, t->cx + 1, erase_attr(t));
		else
			clear_cells(t, t->cy, 0, t->cols, erase_attr(t));
		break;
	case 'L':
		if (t->cy >= t->top && t->cy <= t->bot)
			scroll(t, t->cy, t->bot, -n);
		t->cx = 0;
		break;
	case 'M':
		if (t->cy >= t->top && t->cy <= t->bot)
			scroll(t, t->cy, t->bot, n);
		t->cx = 0;
		break;
	case 'P': delete_cells(t, n); break;
	case 'S': scroll(t, t->top, t->bot, n); break;
	case 'T': if (t->nparam <= 1) scroll(t, t->top, t->bot, -n); break;
	case 'X': clear_cells(t, t->cy, t->cx, t->cx + n, erase_attr(t)); break;
	case 'Z':
		while (n--) {
			do
				t->cx--;
			while (t->cx > 0 && !t->tabs[t->cx]);
		}
		break;
	case 'b': {
		/* repeat the previous character */
		int prev = t->cx > 0 ? t->cx - 1 : 0;
		u16 g = t->glyph[t->cy][prev];

		for (i = 0; i < n && i < t->cols; i++) {
			u32 cp = font_uni_cp[g];

			put_char(t, cp);
		}
		break;
	}
	case 'c':
		if (t->priv == '>')
			reply(t, "\033[>0;276;0c");
		else if (!t->priv)
			reply(t, "\033[?62;22c");
		break;
	case 'd': t->cy = n - 1 + (t->origin ? t->top : 0); break;
	case 'g':
		if (P(t, 0, 0) == 3)
			memset(t->tabs, 0, sizeof(t->tabs));
		else if (t->cx < TERM_MAXCOLS)
			t->tabs[t->cx] = 0;
		break;
	case 'h': set_mode(t, 1); break;
	case 'l': set_mode(t, 0); break;
	case 'm': if (!t->priv) sgr(t); break;
	case 'n':
		if (P(t, 0, 0) == 5) {
			reply(t, "\033[0n");
		} else if (P(t, 0, 0) == 6) {
			ksnprintf(buf, sizeof(buf), "\033[%d;%dR",
				  t->cy + 1 - (t->origin ? t->top : 0), t->cx + 1);
			reply(t, buf);
		}
		break;
	case 'r':
		if (t->priv)
			break;
		{
			int top = P(t, 0, 1) - 1, bot = P(t, 1, t->rows) - 1;

			if (bot >= t->rows)
				bot = t->rows - 1;
			if (top < bot) {
				t->top = top;
				t->bot = bot;
				t->cx = 0;
				t->cy = t->origin ? top : 0;
			}
		}
		break;
	case 's': if (!t->priv) save_cursor(t); break;
	case 'u': if (!t->priv) restore_cursor(t); break;
	case 't':
		if (P(t, 0, 0) == 18) {
			ksnprintf(buf, sizeof(buf), "\033[8;%d;%dt", t->rows, t->cols);
			reply(t, buf);
		}
		break;
	}
	clamp_cursor(t);
	if (c != 'm' && c != 'n' && c != 'c' && c != 'h' && c != 'l' && c != 't')
		t->wrap_pending = 0;
}

static void osc(struct term *t)
{
	int n = 0, i = 0;

	t->osc[t->nosc] = 0;
	while (t->osc[i] >= '0' && t->osc[i] <= '9')
		n = n * 10 + t->osc[i++] - '0';
	if (t->osc[i] != ';')
		return;
	if (n == 0 || n == 2)
		win_set_title(t->w, t->osc + i + 1);
}

static void esc(struct term *t, char c)
{
	switch (c) {
	case '7': save_cursor(t); break;
	case '8': restore_cursor(t); break;
	case 'D': linefeed(t); break;
	case 'E': t->cx = 0; linefeed(t); break;
	case 'H': if (t->cx < TERM_MAXCOLS) t->tabs[t->cx] = 1; break;
	case 'M': reverse_index(t); break;
	case 'c': term_reset(t); break;
	case '=': t->app_keypad = 1; break;
	case '>': t->app_keypad = 0; break;
	}
	t->wrap_pending = 0;
}

static void esc_inter(struct term *t, char c)
{
	switch (t->inter) {
	case '(':
	case ')':
		t->charset[t->inter == ')'] = c == '0';
		break;
	case '#':
		if (c == '8') {		/* DECALN: fill with E */
			int i, j;

			for (i = 0; i < t->rows; i++) {
				for (j = 0; j < t->cols; j++) {
					t->glyph[i][j] = glyph_of['E'];
					t->attr[i][j] = 0;
				}
				touch(t, i, 0, t->cols);
			}
		}
		break;
	}
}

static void control(struct term *t, u8 c)
{
	switch (c) {
	case '\a': break;
	case '\b':
		if (t->cx > 0)
			t->cx--;
		t->wrap_pending = 0;
		break;
	case '\t':
		while (t->cx < t->cols - 1 && !t->tabs[++t->cx])
			;
		t->wrap_pending = 0;
		break;
	case '\n': case '\v': case '\f':
		linefeed(t);
		t->wrap_pending = 0;
		break;
	case '\r':
		t->cx = 0;
		t->wrap_pending = 0;
		break;
	case 0x0e: t->gl = 1; break;	/* SO */
	case 0x0f: t->gl = 0; break;	/* SI */
	case 0x18: case 0x1a: t->state = S_GROUND; break;
	case 0x1b:
		t->state = S_ESC;
		t->inter = 0;
		break;
	}
}

static void parse(struct term *t, u8 c)
{
	if (c < 0x20 && c != 0x1b && t->state != S_OSC && t->state != S_DCS) {
		control(t, c);
		return;
	}
	switch (t->state) {
	case S_GROUND:
		if (c == 0x1b) {
			control(t, c);
		} else if (c < 0x80) {
			t->need = 0;
			if (c != 0x7f)
				put_char(t, c);
		} else if (c < 0xc0) {		/* UTF-8 continuation */
			if (t->need) {
				t->cp = t->cp << 6 | (c & 0x3f);
				if (--t->need == 0)
					put_char(t, t->cp);
			} else {
				put_char(t, 0xfffd);
			}
		} else if (c < 0xe0) {
			t->cp = c & 0x1f;
			t->need = 1;
		} else if (c < 0xf0) {
			t->cp = c & 0x0f;
			t->need = 2;
		} else if (c < 0xf8) {
			t->cp = c & 0x07;
			t->need = 3;
		} else {
			put_char(t, 0xfffd);
		}
		break;
	case S_ESC:
		if (c == '[') {
			t->state = S_CSI;
			t->nparam = 0;
			t->param[0] = -1;
			t->priv = t->inter = 0;
		} else if (c == ']') {
			t->state = S_OSC;
			t->nosc = 0;
		} else if (c == 'P' || c == '_' || c == '^' || c == 'X') {
			t->state = S_DCS;
		} else if (c >= 0x20 && c <= 0x2f) {
			t->inter = c;
			t->state = S_ESC_INTER;
		} else {
			t->state = S_GROUND;
			esc(t, c);
		}
		break;
	case S_ESC_INTER:
		if (c >= 0x20 && c <= 0x2f)
			break;
		t->state = S_GROUND;
		esc_inter(t, c);
		break;
	case S_CSI:
		if (c >= '0' && c <= '9') {
			if (!t->nparam)
				t->nparam = 1;
			if (t->param[t->nparam - 1] < 0)
				t->param[t->nparam - 1] = 0;
			if (t->param[t->nparam - 1] < 10000)
				t->param[t->nparam - 1] = t->param[t->nparam - 1] * 10 + c - '0';
		} else if (c == ';' || c == ':') {
			if (!t->nparam)
				t->nparam = 1;
			if (t->nparam < 16)
				t->param[t->nparam++] = -1;
		} else if (c >= '<' && c <= '?') {
			t->priv = c;
		} else if (c >= 0x20 && c <= 0x2f) {
			t->inter = c;
		} else if (c >= 0x40 && c <= 0x7e) {
			t->state = S_GROUND;
			csi(t, c);
		} else {
			t->state = S_GROUND;
		}
		break;
	case S_OSC:
		if (c == 0x07) {
			t->state = S_GROUND;
			osc(t);
		} else if (c == 0x1b) {
			t->state = S_OSC_ESC;
		} else if (t->nosc < (int)sizeof(t->osc) - 1) {
			t->osc[t->nosc++] = c;
		}
		break;
	case S_OSC_ESC:
		t->state = S_GROUND;
		if (c == '\\')
			osc(t);
		break;
	case S_DCS:
		if (c == 0x1b)
			t->state = S_DCS_ESC;
		else if (c == 0x07)
			t->state = S_GROUND;
		break;
	case S_DCS_ESC:
		t->state = c == '\\' ? S_GROUND : S_DCS;
		break;
	}
}

void term_putc(struct term *t, char c)
{
	parse(t, (u8)c);
}

void term_write(struct term *t, const char *s, int n)
{
	while (n--)
		parse(t, (u8)*s++);
}

void term_puts(struct term *t, const char *s)
{
	while (*s) {
		if (*s == '\n')
			parse(t, '\r');
		parse(t, (u8)*s++);
	}
}

static void tout(void *arg, char c)
{
	if (c == '\n')
		parse(arg, '\r');
	parse(arg, (u8)c);
}

void term_printf(struct term *t, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vformat(tout, t, fmt, ap);
	va_end(ap);
}

void term_reset(struct term *t)
{
	int i;

	if (t->alt)
		set_alt(t, 0, 0);
	t->cx = t->cy = t->wrap_pending = 0;
	t->top = 0;
	t->bot = t->rows - 1;
	t->autowrap = 1;
	t->insert = t->origin = t->cursor_hidden = t->app_cursor = 0;
	t->app_keypad = t->bracketed_paste = t->mouse_mode = 0;
	t->sgr = t->reverse = t->bg_dark = 0;
	t->charset[0] = t->charset[1] = t->gl = 0;
	for (i = 0; i < TERM_MAXCOLS; i++)
		t->tabs[i] = i && !(i & 7);
	save_cursor(t);
	for (i = 0; i < t->rows; i++)
		clear_cells(t, i, 0, t->cols, 0);
	t->state = S_GROUND;
	t->need = 0;
}

void term_redraw_all(struct term *t)
{
	mark_all(t);
}

/* ---- keys ---- */

int term_keyseq(struct term *t, const struct event *e, char *out)
{
	static const char *const fkeys[] = {
		"\033OP", "\033OQ", "\033OR", "\033OS", "\033[15~", "\033[17~",
		"\033[18~", "\033[19~", "\033[20~", "\033[21~", "\033[23~",
		"\033[24~", "\033[25~", "\033[26~", "\033[28~",
	};
	const char *s = NULL;
	int ch = e->ch, n = 0;

	switch (ch) {
	case K_UP: s = t->app_cursor ? "\033OA" : "\033[A"; break;
	case K_DOWN: s = t->app_cursor ? "\033OB" : "\033[B"; break;
	case K_RIGHT: s = t->app_cursor ? "\033OC" : "\033[C"; break;
	case K_LEFT: s = t->app_cursor ? "\033OD" : "\033[D"; break;
	case K_HOME: s = t->app_cursor ? "\033OH" : "\033[H"; break;
	case K_END: s = t->app_cursor ? "\033OF" : "\033[F"; break;
	case K_PGUP: s = "\033[5~"; break;
	case K_PGDN: s = "\033[6~"; break;
	case K_FDEL: s = "\033[3~"; break;
	case K_HELP: s = "\033[2~"; break;
	case '\b': s = "\177"; break;
	default:
		if (ch >= K_F1 && ch < K_F1 + 15)
			s = fkeys[ch - K_F1];
	}
	if (s) {
		while (*s)
			out[n++] = *s++;
		return n;
	}
	if (!ch)
		return 0;
	if (e->mods & MOD_OPTION)
		out[n++] = 0x1b;	/* Option is Meta */
	out[n++] = ch;
	return n;
}

/* ---- drawing ---- */

/* Columns [c0, c1) of a row, a pixel row at a time, each assembled six
 * bits per character into longs. Handles every attribute. */
static void draw_span(struct term *t, int row, int c0, int c1)
{
	struct bitmap *bm = &t->w->bm;
	int x = t->x0 + c0 * CW, xe = t->x0 + c1 * CW, i;
	u32 *dst = (u32 *)(bm->base + (t->y0 + row * CH) * bm->rowbytes) + (x >> 5);
	u32 lm = ~0u >> (x & 31), rm = ~0u << (31 - ((xe - 1) & 31));
	const u16 *gl = t->glyph[row];
	const u8 *at = t->attr[row];

	if ((x >> 5) == ((xe - 1) >> 5))
		lm &= rm;			/* all in one long */
	for (i = 0; i < CH; i++) {
		const u8 *ft = ftab + i * nglyph;
		u32 acc = 0, *d = dst;
		int nb = x & 31, k;

		for (k = c0; k < c1; k++) {
			u32 v = ft[gl[k]];
			u8 a = at[k];

			if (a) {
				if (a & A_BOLD) {
					u32 b = v | v >> 1;

					v = (v & 1) ? b : b & ~1u;
				}
				if ((a & A_UNDER) && i == CH - 2)
					v = 0x3f;
				if ((a & A_STRIKE) && i == CH / 2)
					v = 0x3f;
				if (a & A_INV)
					v ^= 0x3f;
			}
			if (nb <= 26) {
				acc |= v << (26 - nb);
				nb += 6;
				if (nb == 32) {
					*d = d == dst ? (*d & ~lm) | (acc & lm) : acc;
					d++;
					acc = 0;
					nb = 0;
				}
			} else {
				acc |= v >> (nb - 26);
				*d = d == dst ? (*d & ~lm) | (acc & lm) : acc;
				d++;
				nb -= 26;
				acc = v << (32 - nb);
			}
		}
		if (nb) {
			u32 m = d == dst ? lm : rm;

			*d = (*d & ~m) | (acc & m);
		}
		dst = (u32 *)((u8 *)dst + bm->rowbytes);
	}
}

/* Draw a row's dirty columns: plain groups of 16 with text_rows, the
 * rest with draw_span. */
static void draw_row(struct term *t, int row, int c0, int c1)
{
	struct bitmap *bm = &t->w->bm;
	int full = t->cols / 16, g, run = -1;
	const u8 *at = t->attr[row];

	if (t->x0 & 31) {
		draw_span(t, row, c0, c1);
		return;
	}
	for (g = c0 / 16; g <= full; g++) {
		int plain = 0, i;

		if (g < full && g * 16 < c1) {
			plain = 1;
			for (i = g * 16; i < g * 16 + 16; i++)
				if (at[i]) {
					plain = 0;
					break;
				}
		}
		if (plain) {
			if (run < 0)
				run = g;
			continue;
		}
		if (run >= 0) {
			text_rows((u32 *)(bm->base + (t->y0 + row * CH) * bm->rowbytes) +
				  (t->x0 >> 5) + run * 3, t->glyph[row] + run * 16,
				  g - run, ftab, nglyph, bm->rowbytes, CH);
			run = -1;
		}
		if (g * 16 >= c1)
			break;
		{
			int s = g * 16, e = g * 16 + 16;

			if (e > t->cols)
				e = t->cols;
			if (s < e)
				draw_span(t, row, s, e);
		}
	}
}

static void render(struct window *w)
{
	struct term *t = w->data;
	int i, cx = t->cx < t->cols ? t->cx : t->cols - 1;

	if (t->ccy >= 0 && (t->ccx != cx || t->ccy != t->cy || t->cursor_hidden ||
			     t->hi[t->ccy] || t->sc_n))
		hide_cursor(t);
	apply_scroll(t);
	for (i = 0; i < t->rows; i++) {
		if (!t->hi[i])
			continue;
		draw_row(t, i, t->lo[i], t->hi[i]);
		t->hi[i] = 0;
		inval_rows(t, i, i);
	}
	if (t->ccy < 0 && !t->cursor_hidden) {
		xor_cursor(t, cx, t->cy);
		t->ccx = cx;
		t->ccy = t->cy;
	}
}

/* Give the grid a new size, keeping what fits: the bottom rows if it
 * shrinks, so the cursor's line stays. */
static void resize_grid(struct term *t, int cols, int rows)
{
	int rc = (cols + 15) & ~15, i, drop = 0;
	u16 *g[TERM_MAXROWS], *ag[TERM_MAXROWS];
	u8 *a[TERM_MAXROWS], *aa[TERM_MAXROWS];

	if (cols > TERM_MAXCOLS)
		cols = TERM_MAXCOLS;
	if (rows > TERM_MAXROWS)
		rows = TERM_MAXROWS;
	if (t->cy >= rows)
		drop = t->cy - rows + 1;
	for (i = 0; i < rows; i++) {
		int src = i + drop, j, n = cols < t->cols ? cols : t->cols;

		g[i] = kmalloc(rc * sizeof(u16));
		a[i] = kzalloc(rc);
		ag[i] = kmalloc(rc * sizeof(u16));
		aa[i] = kzalloc(rc);
		for (j = 0; j < rc; j++)
			g[i][j] = ag[i][j] = g_space;
		if (src < t->rows) {
			memcpy(g[i], t->glyph[src], n * sizeof(u16));
			memcpy(a[i], t->attr[src], n);
			memcpy(ag[i], t->alt_glyph[src], n * sizeof(u16));
			memcpy(aa[i], t->alt_attr[src], n);
		}
	}
	for (i = 0; i < t->rows; i++) {
		kfree(t->glyph[i]);
		kfree(t->attr[i]);
		kfree(t->alt_glyph[i]);
		kfree(t->alt_attr[i]);
	}
	for (i = 0; i < rows; i++) {
		t->glyph[i] = g[i];
		t->attr[i] = a[i];
		t->alt_glyph[i] = ag[i];
		t->alt_attr[i] = aa[i];
		t->hi[i] = 0;
	}
	t->cols = cols;
	t->rows = rows;
	t->cy -= drop;
	t->top = 0;
	t->bot = rows - 1;
	t->wrap_pending = 0;
	clamp_cursor(t);
	t->saved.cy = t->saved.cy >= rows ? rows - 1 : t->saved.cy;
	t->saved.cx = t->saved.cx >= cols ? cols - 1 : t->saved.cx;
}

/* The window has a new bitmap: fit the grid to it and draw everything. */
static void resized(struct window *w)
{
	struct term *t = w->data;
	int margin = w->zoomed ? 0 : 2;

	t->x0 = w->content.left + margin;
	t->y0 = w->content.top + 2;
	t->ccy = -1;
	t->sc_n = 0;
	resize_grid(t, (w->content.right - t->x0 - margin) / CW,
		    (w->content.bottom - t->y0 - 2) / CH);
	mark_all(t);
	if (t->session_resized)
		t->session_resized(t);
}

void term_zoom(struct term *t)
{
	win_zoom(t->w);
}

static void event(struct window *w, struct event *e)
{
	struct term *t = w->data;

	if (e->type == EV_KEY && t->key)
		t->key(t, e);
}

static void closed(struct window *w)
{
	struct term *t = w->data;
	int i;

	if (t->session_close)
		t->session_close(t);
	for (i = 0; i < t->rows; i++) {
		kfree(t->glyph[i]);
		kfree(t->attr[i]);
		kfree(t->alt_glyph[i]);
		kfree(t->alt_attr[i]);
	}
	if (t->data)
		kfree(t->data);
	kfree(t);
}

struct term *term_new(int x, int y, int cols, int rows, const char *title)
{
	struct term *t = kzalloc(sizeof(*t));
	int i, rc;

	if (!t)
		return NULL;
	if (!ftab)
		fonts_init();
	if (cols > TERM_MAXCOLS)
		cols = TERM_MAXCOLS;
	if (rows > TERM_MAXROWS)
		rows = TERM_MAXROWS;
	t->cols = cols;
	t->rows = rows;
	rc = (cols + 15) & ~15;
	for (i = 0; i < rows; i++) {
		t->glyph[i] = kmalloc(rc * sizeof(u16));
		t->attr[i] = kzalloc(rc);
		t->alt_glyph[i] = kmalloc(rc * sizeof(u16));
		t->alt_attr[i] = kzalloc(rc);
	}
	t->ccy = -1;
	t->w = win_new(x, y, cols * CW + 4, rows * CH + 4, title);
	if (!t->w) {
		struct window tmp = { .data = t };

		t->data = NULL;
		closed(&tmp);
		return NULL;
	}
	t->x0 = t->w->content.left + 2;
	t->y0 = t->w->content.top + 2;
	t->w->data = t;
	t->w->event = event;
	t->w->render = render;
	t->w->close = closed;
	t->w->resized = resized;
	set_alt(t, 1, 1);		/* clear both screens */
	set_alt(t, 0, 1);
	term_reset(t);
	return t;
}

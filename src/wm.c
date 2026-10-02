/* wm.c - windows, the desktop and the menu bar.
 *
 * Every window keeps all of its pixels in its own bitmap, so the screen
 * can always be rebuilt from the windows' bitmaps without asking anyone
 * to redraw: bringing a window forward or moving it is just blitting.
 * Applications draw into their window's bitmap, mark what they changed
 * with win_invalidate(), and wm_flush() copies the changes to whatever
 * part of the window is visible.
 */
#include "kernel.h"
#include "wm.h"

#define MAX_MENUS	6

struct menu {
	const char *title;
	const struct menu_item *items;
	int n;
	struct rect tr;			/* title in the menu bar */
	int width;			/* of the pulled-down list */
};

static struct window *front;		/* frontmost window */
static struct region desk;		/* uncovered desktop, screen coords */
static struct rect desk_area;		/* everything below the menu bar */
static struct menu menus[MAX_MENUS];
static int nmenus, menu_x = 10;

/* The command key symbol, 8 pixels wide */
static const u8 cmd_rows[1][13] = {
	{ 0, 0, 0, 0x44, 0xaa, 0x7c, 0x28, 0x7c, 0xaa, 0x44, 0, 0, 0 },
};
static const struct font cmd_font = { 8, 13, 10, cmd_rows };

/* ---- drawing on the screen, keeping clear of the mouse pointer ---- */

/* Big copies go in bands so that the pointer, if it is in the way, is
 * only lifted for the band it overlaps and is never gone for long. */
#define BAND 32

static void screen_fill(const struct rect *r, const u8 *pat, int mode)
{
	struct rect b = *r;

	for (; b.top < r->bottom; b.top = b.bottom) {
		b.bottom = b.top + BAND < r->bottom ? b.top + BAND : r->bottom;
		cursor_shield(b.left, b.top, b.right, b.bottom);
		fill_rect(&screen, &b, pat, mode, NULL);
		cursor_unshield();
	}
}

static void screen_blit(const struct bitmap *src, const struct rect *sr,
			int dx, int dy)
{
	struct rect b = *sr;

	for (; b.top < sr->bottom; b.top = b.bottom) {
		int y = dy + b.top - sr->top;

		b.bottom = b.top + BAND < sr->bottom ? b.top + BAND : sr->bottom;
		cursor_shield(dx, y, dx + b.right - b.left, y + b.bottom - b.top);
		copy_bits(src, &b, &screen, dx, y, M_COPY, NULL);
		cursor_unshield();
	}
}

static void text_bold(const struct bitmap *bm, int x, int y, const char *s,
		      const struct rect *clip)
{
	draw_text(bm, x, y, s, &font_6x13, M_OR, clip);
	draw_text(bm, x + 1, y, s, &font_6x13, M_OR, clip);
}

/* ---- window geometry ---- */

/* The window's opaque shape: its box plus a one pixel drop shadow. */
static int shape(const struct window *w, struct rect *r)
{
	const struct rect *f = &w->frame;

	if (w->zoomed) {
		r[0] = *f;
		return 1;
	}
	r[0] = mkrect(f->left, f->top, f->right - 1, f->bottom - 1);
	r[1] = mkrect(f->right - 1, f->top + 1, f->right, f->bottom);
	r[2] = mkrect(f->left + 1, f->bottom - 1, f->right - 1, f->bottom);
	return 3;
}

static int win_at(const struct window *w, int x, int y)
{
	struct rect r[3];
	int i, n = shape(w, r);

	for (i = 0; i < n; i++)
		if (rect_has(&r[i], x, y))
			return 1;
	return 0;
}

/* Work out what of each window, and of the desktop, can be seen. */
static void compute_visibility(void)
{
	struct window *w, *v;
	struct rect r[3];
	int i, n;

	region_set(&desk, &desk_area);
	for (w = front; w; w = w->next) {
		struct rect all = mkrect(0, 0, screen.width, screen.height);

		n = shape(w, r);
		w->vis.n = 0;
		for (i = 0; i < n; i++) {
			struct rect s;

			/* only a zoomed window may cover the menu bar */
			if (rect_sect(&r[i], w->zoomed ? &all : &desk_area, &s))
				region_add(&w->vis, &s);
		}
		for (v = front; v != w; v = v->next) {
			struct rect q[3];
			int j, m = shape(v, q);

			for (j = 0; j < m; j++)
				region_sub(&w->vis, &q[j]);
		}
		for (i = 0; i < n; i++)
			region_sub(&desk, &r[i]);
	}
}

/* Copy the part of w that belongs at r on the screen. */
static void blit_win(struct window *w, const struct rect *r)
{
	struct rect s = *r;

	rect_offset(&s, w->ox - w->frame.left, -w->frame.top);
	screen_blit(&w->bm, &s, r->left, r->top);
}

/* Rebuild the screen inside area from the desktop and window bitmaps. */
static void paint(const struct rect *area)
{
	struct window *w;
	struct rect s;
	int i;

	for (i = 0; i < desk.n; i++)
		if (rect_sect(&desk.r[i], area, &s))
			screen_fill(&s, pat_gray, M_COPY);
	for (w = front; w; w = w->next)
		for (i = 0; i < w->vis.n; i++)
			if (rect_sect(&w->vis.r[i], area, &s))
				blit_win(w, &s);
}

/* ---- window chrome ---- */

static void draw_chrome(struct window *w)
{
	struct bitmap *bm = &w->bm;
	int x = w->ox, W = w->frame.right - w->frame.left;
	int H = w->frame.bottom - w->frame.top, tw, tx, y;
	struct rect r;

	r = mkrect(x, 0, x + W - 1, H - 1);
	frame_rect(bm, &r, M_OR, NULL);
	r = mkrect(x + W - 1, 1, x + W, H);
	fill_rect(bm, &r, pat_black, M_COPY, NULL);
	r = mkrect(x + 1, H - 1, x + W, H);
	fill_rect(bm, &r, pat_black, M_COPY, NULL);

	/* title bar */
	r = mkrect(x + 1, 1, x + W - 2, TITLE_H - 1);
	fill_rect(bm, &r, pat_white, M_COPY, NULL);
	r = mkrect(x + 1, TITLE_H - 1, x + W - 2, TITLE_H);
	fill_rect(bm, &r, pat_black, M_COPY, NULL);
	tw = text_width(w->title, &font_6x13) + 1;
	tx = x + (W - tw) / 2;
	if (w == front) {
		for (y = 4; y <= 14; y += 2) {
			r = mkrect(x + 2, y, x + W - 3, y + 1);
			fill_rect(bm, &r, pat_black, M_COPY, NULL);
		}
		/* close box */
		r = mkrect(x + 7, 3, x + 20, 16);
		fill_rect(bm, &r, pat_white, M_COPY, NULL);
		r = mkrect(x + 8, 4, x + 19, 15);
		frame_rect(bm, &r, M_OR, NULL);
		/* clear a space for the title */
		r = mkrect(tx - 7, 2, tx + tw + 7, TITLE_H - 2);
		fill_rect(bm, &r, pat_white, M_COPY, NULL);
	}
	text_bold(bm, tx, 3, w->title, NULL);
	r = mkrect(x, 0, x + W, TITLE_H);
	win_invalidate(w, &r);
}

static const struct rect close_box = { 8, 4, 19, 15 };

/* ---- windows ---- */

void win_invalidate(struct window *w, const struct rect *r)
{
	rect_union(&w->dirty, r);
}

void win_set_title(struct window *w, const char *title)
{
	int i;

	for (i = 0; title[i] && i < (int)sizeof(w->title) - 1; i++)
		w->title[i] = title[i] >= ' ' && title[i] < 0x7f ? title[i] : '?';
	w->title[i] = 0;
	draw_chrome(w);
}

struct window *win_frontmost(void)
{
	return front;
}

struct window *win_new(int x, int y, int cw, int ch, const char *title)
{
	struct window *w = kzalloc(sizeof(*w)), *old = front;
	int W = cw + 3, H = ch + TITLE_H + 2;

	if (!w)
		return NULL;
	w->ox = WIN_PAD;
	if (!bitmap_alloc(&w->bm, WIN_PAD + W, H)) {
		kfree(w);
		return NULL;
	}
	strcpy(w->title, title);
	x = WIN_SNAP(x);
	w->frame = mkrect(x, y, x + W, y + H);
	w->content = mkrect(WIN_PAD + 1, TITLE_H, WIN_PAD + W - 2, H - 2);
	w->next = front;
	front = w;
	if (old)
		draw_chrome(old);
	draw_chrome(w);
	compute_visibility();
	w->dirty = mkrect(0, 0, 0, 0);
	paint(&w->frame);
	return w;
}

void win_front(struct window *w)
{
	struct window **pp, *old = front;

	if (w == front)
		return;
	for (pp = &front; *pp != w; pp = &(*pp)->next)
		;
	*pp = w->next;
	w->next = front;
	front = w;
	draw_chrome(old);
	draw_chrome(w);
	compute_visibility();
	w->dirty = mkrect(0, 0, 0, 0);
	paint(&w->frame);
}

void win_close(struct window *w)
{
	struct window **pp;
	struct rect f = w->frame;

	for (pp = &front; *pp != w; pp = &(*pp)->next)
		;
	*pp = w->next;
	if (w->close)
		w->close(w);
	bitmap_free(&w->bm);
	kfree(w);
	if (front)
		draw_chrome(front);
	compute_visibility();
	paint(&f);
}

void win_move(struct window *w, int x, int y)
{
	struct rect old = w->frame;

	rect_offset(&w->frame, x - old.left, y - old.top);
	compute_visibility();
	paint(&w->frame);
	paint(&old);
}

/* Fill the screen with w, frame and all, or put it back. The window's
 * bitmap is new afterwards: its owner redraws in w->resized. */
void win_zoom(struct window *w)
{
	struct bitmap nb, old = w->bm;
	int W, H, ox;

	if (!w->zoomed) {
		w->saved_frame = w->frame;
		w->frame = mkrect(0, 0, screen.width, screen.height);
		ox = 32;		/* the screen's left edge on a long */
	} else {
		w->frame = w->saved_frame;
		ox = WIN_PAD;
	}
	W = w->frame.right - w->frame.left;
	H = w->frame.bottom - w->frame.top;
	if (!bitmap_alloc(&nb, ox + W, H)) {
		w->frame = w->zoomed ? w->saved_frame : w->frame;
		return;
	}
	w->bm = nb;
	bitmap_free(&old);
	w->ox = ox;
	w->zoomed = !w->zoomed;
	if (w->zoomed) {
		w->content = mkrect(ox, 0, ox + W, H);
	} else {
		w->content = mkrect(ox + 1, TITLE_H, ox + W - 2, H - 2);
		draw_chrome(w);
	}
	w->dirty = mkrect(0, 0, 0, 0);
	if (w->resized)
		w->resized(w);
	if (w->render)
		w->render(w);
	w->dirty = mkrect(0, 0, 0, 0);
	if (w->zoomed) {
		compute_visibility();
		paint(&w->frame);
	} else {
		wm_redraw();
	}
}

/* Copy everything applications have changed onto the screen. */
void wm_flush(void)
{
	struct window *w;

	for (w = front; w; w = w->next) {
		struct rect d, s;
		int i;

		if (w->render)
			w->render(w);
		d = w->dirty;
		if (rect_empty(&d))
			continue;
		w->dirty = mkrect(0, 0, 0, 0);
		rect_offset(&d, w->frame.left - w->ox, w->frame.top);
		for (i = 0; i < w->vis.n; i++)
			if (rect_sect(&w->vis.r[i], &d, &s))
				blit_win(w, &s);
	}
}

/* ---- dragging ---- */

static void xor_outline(const struct rect *r)
{
	struct rect e[4];
	int i;

	e[0] = mkrect(r->left, r->top, r->right, r->top + 1);
	e[1] = mkrect(r->left, r->bottom - 1, r->right, r->bottom);
	e[2] = mkrect(r->left, r->top + 1, r->left + 1, r->bottom - 1);
	e[3] = mkrect(r->right - 1, r->top + 1, r->right, r->bottom - 1);
	cursor_shield(r->left, r->top, r->right, r->bottom);
	for (i = 0; i < 4; i++)
		fill_rect(&screen, &e[i], pat_gray, M_XOR, &desk_area);
	cursor_unshield();
}

/* Drag the outline of w until the button comes up, then move w. */
static void drag(struct window *w)
{
	int mx = mouse_x, my = mouse_y, dx = 0, dy = 0;
	struct rect o = mkrect(w->frame.left, w->frame.top, w->frame.right - 1,
			       w->frame.bottom - 1), r = o;

	xor_outline(&r);
	while (mouse_button) {
		int nx = WIN_SNAP(o.left + mouse_x - mx) - o.left, ny = mouse_y - my;

		/* keep some of the title bar on the screen */
		if (o.top + ny < MENUBAR_H)
			ny = MENUBAR_H - o.top;
		if (nx != dx || ny != dy) {
			xor_outline(&r);
			dx = nx, dy = ny;
			r = o;
			rect_offset(&r, dx, dy);
			xor_outline(&r);
		}
		cpu_idle();
	}
	xor_outline(&r);
	if (dx || dy)
		win_move(w, w->frame.left + dx, w->frame.top + dy);
}

/* Track a click in the close box: 1 if released inside it. */
static int track_close(struct window *w)
{
	struct rect r = close_box, ir = close_box;
	int in = 0;

	rect_inset(&ir, 1, 1);
	rect_offset(&r, w->frame.left, w->frame.top);
	rect_offset(&ir, w->frame.left, w->frame.top);
	while (mouse_button) {
		int now = rect_has(&r, mouse_x, mouse_y);

		if (now != in) {
			screen_fill(&ir, pat_black, M_XOR);
			in = now;
		}
		cpu_idle();
	}
	if (in)
		screen_fill(&ir, pat_black, M_XOR);
	return in;
}

/* ---- menus ---- */

void wm_add_menu(const char *title, const struct menu_item *items, int n)
{
	struct menu *m = &menus[nmenus++];
	int i, tw = text_width(title, &font_6x13) + 1;

	m->title = title;
	m->items = items;
	m->n = n;
	m->tr = mkrect(menu_x - 6, 0, menu_x + tw + 6, MENUBAR_H - 1);
	menu_x += tw + 12;
	m->width = 0;
	for (i = 0; i < n; i++) {
		int w = text_width(items[i].label, &font_6x13) + 30 +
			(items[i].key ? 24 : 0);

		if (w > m->width)
			m->width = w;
	}
}

static void draw_menubar(void)
{
	struct rect r = mkrect(0, 0, screen.width, MENUBAR_H - 1);
	static const u8 corner[5] = { 0xf8, 0xe0, 0xc0, 0x80, 0x80 };
	int i;

	screen_fill(&r, pat_white, M_COPY);
	r = mkrect(0, MENUBAR_H - 1, screen.width, MENUBAR_H);
	screen_fill(&r, pat_black, M_COPY);
	cursor_shield(0, 0, screen.width, MENUBAR_H);
	for (i = 0; i < nmenus; i++)
		text_bold(&screen, menus[i].tr.left + 6, 3, menus[i].title, NULL);
	/* the screen's rounded top corners */
	for (i = 0; i < 5; i++) {
		int j;

		for (j = 0; j < 5; j++) {
			if (!(corner[i] & (0x80 >> j)))
				continue;
			r = mkrect(j, i, j + 1, i + 1);
			fill_rect(&screen, &r, pat_black, M_COPY, NULL);
			r = mkrect(screen.width - 1 - j, i, screen.width - j, i + 1);
			fill_rect(&screen, &r, pat_black, M_COPY, NULL);
		}
	}
	cursor_unshield();
}

#define ITEM_H 16

static struct rect menu_box(const struct menu *m)
{
	return mkrect(m->tr.left, MENUBAR_H - 1, m->tr.left + m->width,
		      MENUBAR_H - 1 + m->n * ITEM_H + 2);
}

static void draw_menu(const struct menu *m, struct bitmap *save)
{
	struct rect b = menu_box(m), s;
	int i;

	b.right++, b.bottom++;		/* shadow */
	bitmap_alloc(save, b.right - b.left, b.bottom - b.top);
	cursor_shield(b.left, b.top, b.right, b.bottom);
	s = b;
	copy_bits(&screen, &s, save, 0, 0, M_COPY, NULL);
	b.right--, b.bottom--;
	fill_rect(&screen, &b, pat_white, M_COPY, NULL);
	frame_rect(&screen, &b, M_OR, NULL);
	s = mkrect(b.right, b.top + 2, b.right + 1, b.bottom + 1);
	fill_rect(&screen, &s, pat_black, M_COPY, NULL);
	s = mkrect(b.left + 2, b.bottom, b.right + 1, b.bottom + 1);
	fill_rect(&screen, &s, pat_black, M_COPY, NULL);
	for (i = 0; i < m->n; i++) {
		int y = b.top + 1 + i * ITEM_H;
		const struct menu_item *it = &m->items[i];

		if (it->label[0] == '-') {
			s = mkrect(b.left + 1, y + ITEM_H / 2, b.right - 1,
				   y + ITEM_H / 2 + 1);
			fill_rect(&screen, &s, pat_gray, M_OR, NULL);
			continue;
		}
		draw_text(&screen, b.left + 14, y + 2, it->label, &font_6x13,
			  M_OR, NULL);
		if (it->key) {
			char k[2] = { it->key, 0 };

			draw_char(&screen, b.right - 24, y + 2, 0, &cmd_font,
				  M_OR, NULL);
			draw_text(&screen, b.right - 14, y + 2, k, &font_6x13,
				  M_OR, NULL);
		}
	}
	cursor_unshield();
}

static void undraw_menu(const struct menu *m, struct bitmap *save)
{
	struct rect b = menu_box(m), s;

	b.right++, b.bottom++;
	s = mkrect(0, 0, b.right - b.left, b.bottom - b.top);
	screen_blit(save, &s, b.left, b.top);
	bitmap_free(save);
}

static int item_at(const struct menu *m, int x, int y)
{
	struct rect b = menu_box(m);
	int i;

	if (!rect_has(&b, x, y))
		return -1;
	i = (y - b.top - 1) / ITEM_H;
	if (i < 0 || i >= m->n || m->items[i].label[0] == '-')
		return -1;
	return i;
}

static void invert_item(const struct menu *m, int i)
{
	struct rect b = menu_box(m);
	struct rect r = mkrect(b.left + 1, b.top + 1 + i * ITEM_H, b.right - 1,
			       b.top + 1 + (i + 1) * ITEM_H);

	screen_fill(&r, pat_black, M_XOR);
}

static void track_menus(void)
{
	struct bitmap save = { 0 };
	struct menu *open = NULL;
	int item = -1, i;

	while (mouse_button) {
		struct menu *m = open;
		int it;

		if (mouse_y < MENUBAR_H) {
			m = NULL;
			for (i = 0; i < nmenus; i++)
				if (mouse_x >= menus[i].tr.left &&
				    mouse_x < menus[i].tr.right)
					m = &menus[i];
		}
		if (m != open) {
			if (open) {
				undraw_menu(open, &save);
				screen_fill(&open->tr, pat_black, M_XOR);
			}
			open = m;
			item = -1;
			if (open) {
				screen_fill(&open->tr, pat_black, M_XOR);
				draw_menu(open, &save);
			}
		}
		it = open ? item_at(open, mouse_x, mouse_y) : -1;
		if (it != item) {
			if (item >= 0)
				invert_item(open, item);
			if (it >= 0)
				invert_item(open, it);
			item = it;
		}
		cpu_idle();
	}
	if (!open)
		return;
	undraw_menu(open, &save);
	screen_fill(&open->tr, pat_black, M_XOR);
	if (item >= 0 && open->items[item].fn)
		open->items[item].fn();
}

static int menu_key(int ch)
{
	int i, j;

	if (ch >= 'a' && ch <= 'z')
		ch -= 'a' - 'A';
	for (i = 0; i < nmenus; i++) {
		for (j = 0; j < menus[i].n; j++) {
			const struct menu_item *it = &menus[i].items[j];

			if (it->key == ch && it->fn) {
				/* flash the menu's title, as a click would */
				u32 t0 = ticks;

				if (!front || !front->zoomed) {
					screen_fill(&menus[i].tr, pat_black, M_XOR);
					while (ticks - t0 < 4)
						cpu_idle();
					screen_fill(&menus[i].tr, pat_black, M_XOR);
				}
				it->fn();
				return 1;
			}
		}
	}
	return 0;
}

/* ---- events ---- */

void wm_event(struct event *e)
{
	struct window *w;

	switch (e->type) {
	case EV_MOUSE_DOWN:
		if (e->y < MENUBAR_H) {
			track_menus();
			return;
		}
		for (w = front; w; w = w->next)
			if (win_at(w, e->x, e->y))
				break;
		if (!w)
			return;
		if (w != front) {
			win_front(w);
			return;
		}
		e->x -= w->frame.left;
		e->y -= w->frame.top;
		if (!w->zoomed && e->y < TITLE_H) {
			if (rect_has(&close_box, e->x, e->y)) {
				if (track_close(w))
					win_close(w);
			} else {
				drag(w);
			}
			return;
		}
		e->x += w->ox;		/* to the window's bitmap */
		break;
	case EV_KEY:
		if ((e->mods & MOD_CMD) && menu_key(e->ch))
			return;
		break;
	}
	if (front && front->event)
		front->event(front, e);
}

void wm_redraw(void)
{
	struct rect all = mkrect(0, 0, screen.width, screen.height);

	draw_menubar();
	compute_visibility();
	paint(&all);
}

void wm_init(void)
{
	desk_area = mkrect(0, MENUBAR_H, screen.width, screen.height);
	wm_redraw();
}

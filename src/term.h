#ifndef TERM_H
#define TERM_H

#include "wm.h"

#define TERM_MAXCOLS	128
#define TERM_MAXROWS	64

/* Cell attributes */
#define A_BOLD		0x01
#define A_UNDER		0x02
#define A_INV		0x04		/* drawn white on black */
#define A_STRIKE	0x08
#define A_WIDE_R	0x10		/* right half of a double-width character */

struct term {
	struct window *w;
	int cols, rows;
	int x0, y0;			/* top left of the text, in w->bm */

	/* the screen: rows of glyph numbers and attributes */
	u16 *glyph[TERM_MAXROWS];
	u8 *attr[TERM_MAXROWS];
	u16 *alt_glyph[TERM_MAXROWS];	/* the other screen (main or alternate) */
	u8 *alt_attr[TERM_MAXROWS];
	int alt;			/* showing the alternate screen */
	u8 lo[TERM_MAXROWS], hi[TERM_MAXROWS];	/* columns to redraw */

	/* cursor and modes */
	int cx, cy, wrap_pending;
	int top, bot;			/* scrolling region, inclusive */
	int autowrap, insert, origin, cursor_hidden, app_cursor, app_keypad;
	int bracketed_paste, mouse_mode;
	u8 tabs[TERM_MAXCOLS];

	/* current rendition */
	u8 sgr;				/* A_BOLD | A_UNDER | A_STRIKE */
	u8 reverse, bg_dark;
	u8 charset[2], gl;		/* G0/G1: 0 ASCII, 1 DEC line drawing */

	struct {
		int cx, cy, wrap_pending, origin;
		u8 sgr, reverse, bg_dark, charset[2], gl;
	} saved;

	/* bytes in: UTF-8 decoder and escape sequence parser */
	u32 cp;
	int need;
	int state;
	int nparam, param[16];
	char priv, inter;
	char osc[64];
	int nosc;

	/* scrolling not yet applied to the bitmap */
	int sc_top, sc_bot, sc_n;
	int ccx, ccy;			/* where the cursor is drawn, ccy < 0: not */

	void (*key)(struct term *, const struct event *);  /* keystrokes */
	void (*reply)(struct term *, const char *, int);   /* answers to queries */
	void *data;			/* the owner's (the shell's) */
	void *session;			/* a connection using the window */
	void (*session_close)(struct term *);	/* window closing */
	void (*session_resized)(struct term *);	/* new cols and rows */
};

struct term *term_new(int x, int y, int cols, int rows, const char *title);
void term_write(struct term *t, const char *s, int n);
void term_puts(struct term *t, const char *s);	/* \n becomes \r\n */
void term_printf(struct term *t, const char *fmt, ...);
void term_putc(struct term *t, char c);
void term_redraw_all(struct term *t);
void term_reset(struct term *t);
void term_zoom(struct term *t);			/* full screen and back */

/* The bytes a key sends to a program, xterm style: 0 if none. */
int term_keyseq(struct term *t, const struct event *e, char *out);

#endif

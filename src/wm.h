#ifndef WM_H
#define WM_H

#include "gfx.h"
#include "region.h"
#include "input.h"

#define MENUBAR_H	20
#define TITLE_H		19

/* Window bitmaps start this far left of the frame, which puts the first
 * text column of a terminal (frame + 3) on a long boundary. */
#define WIN_PAD		29

/* Windows sit at x = 5 mod 8 on the screen, so that copying a window to
 * the screen moves whole bytes and never has to shift. */
#define WIN_SNAP(x)	((((x) - 5 + 4) & ~7) + 5)

struct window {
	struct window *next;		/* the window behind this one */
	struct rect frame;		/* on the screen, shadow included */
	struct bitmap bm;		/* all of the window's pixels */
	int ox;				/* x of the frame's left edge in bm */
	struct rect content;		/* the application's part, in bm */
	struct region vis;		/* visible pieces, screen coords */
	struct rect dirty;		/* in bm: not yet on the screen */
	char title[48];
	void (*event)(struct window *, struct event *);
	void (*render)(struct window *);	/* before copying to the screen */
	void (*close)(struct window *);
	void (*resized)(struct window *);	/* bm and content are new */
	int zoomed;			/* filling the screen, no frame */
	struct rect saved_frame;	/* where it was before zooming */
	void *data;
};

struct window *win_new(int x, int y, int cw, int ch, const char *title);
void win_close(struct window *w);
void win_front(struct window *w);
void win_move(struct window *w, int x, int y);
void win_zoom(struct window *w);		/* full screen and back */
void win_invalidate(struct window *w, const struct rect *r);	/* bm coords */
void win_set_title(struct window *w, const char *title);
struct window *win_frontmost(void);

void wm_init(void);
void wm_event(struct event *e);
void wm_flush(void);
void wm_redraw(void);

struct menu_item {
	const char *label;
	char key;			/* Cmd-key, or 0 */
	void (*fn)(void);
};

void wm_add_menu(const char *title, const struct menu_item *items, int n);

#endif

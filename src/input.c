/* input.c - mouse and keyboard events from ADB, US key layout. */
#include "kernel.h"
#include "input.h"

#define QSIZE 64			/* power of two */

static struct event queue[QSIZE];
static volatile u32 qhead, qtail;	/* written by irq / by event_get */

volatile int mouse_x, mouse_y, mouse_button;
static int mods;

/* ADB keyboards don't repeat keys; we do, from the VBL interrupt. */
#define REPEAT_DELAY	24		/* ticks before the first repeat */
#define REPEAT_RATE	3		/* ticks between repeats */
static u8 rep_code, rep_ch;
static u32 rep_next;

/* ADB key codes for the modifier and caps lock keys */
#define KC_CMD		0x37
#define KC_SHIFT	0x38
#define KC_CAPS		0x39
#define KC_OPTION	0x3a
#define KC_CTRL		0x36
#define KC_RSHIFT	0x7b
#define KC_ROPTION	0x7c
#define KC_RCTRL	0x7d

/* Unshifted and shifted characters by ADB key code */
static const u8 plain[128] = {
	'a', 's', 'd', 'f', 'h', 'g', 'z', 'x',	/* 00 */
	'c', 'v', 0, 'b', 'q', 'w', 'e', 'r',
	'y', 't', '1', '2', '3', '4', '6', '5',	/* 10 */
	'=', '9', '7', '-', '8', '0', ']', 'o',
	'u', '[', 'i', 'p', '\r', 'l', 'j', '\'',	/* 20 */
	'k', ';', '\\', ',', '/', 'n', 'm', '.',
	'\t', ' ', '`', '\b', '\r', 0x1b, 0, 0,	/* 30 */
	0, 0, 0, K_LEFT, K_RIGHT, K_DOWN, K_UP, 0,
	0, '.', 0, '*', 0, '+', 0, 0x1b,	/* 40: keypad; clear */
	0, 0, 0, '/', '\r', 0, '-', 0,
	0, '=', '0', '1', '2', '3', '4', '5',	/* 50 */
	'6', '7', 0, '8', '9', 0, 0, 0,
	K_F1 + 4, K_F1 + 5, K_F1 + 6, K_F1 + 2,	/* 60: F5 F6 F7 F3 */
	K_F1 + 7, K_F1 + 8, 0, K_F1 + 10,	/* F8 F9 - F11 */
	0, K_F1 + 12, 0, K_F1 + 13,		/* - F13 - F14 */
	0, K_F1 + 9, 0, K_F1 + 11,		/* - F10 - F12 */
	0, K_F1 + 14, K_HELP, K_HOME,		/* 70: - F15 help home */
	K_PGUP, K_FDEL, K_F1 + 3, K_END,	/* pgup del F4 end */
	K_F1 + 1, K_PGDN, K_F1, 0,		/* F2 pgdn F1; 7b-7d are */
	0, 0, 0, 0,				/* right shift/option/ctrl */
};

static const u8 shifted[128] = {
	'A', 'S', 'D', 'F', 'H', 'G', 'Z', 'X',
	'C', 'V', 0, 'B', 'Q', 'W', 'E', 'R',
	'Y', 'T', '!', '@', '#', '$', '^', '%',
	'+', '(', '&', '_', '*', ')', '}', 'O',
	'U', '{', 'I', 'P', '\r', 'L', 'J', '"',
	'K', ':', '|', '<', '?', 'N', 'M', '>',
	'\t', ' ', '~', '\b', '\r', 0x1b, 0, 0,
	0, 0, 0, K_LEFT, K_RIGHT, K_DOWN, K_UP, 0,
	0, '.', 0, '*', 0, '+', 0, 0x1b,
	0, 0, 0, '/', '\r', 0, '-', 0,
	0, '=', '0', '1', '2', '3', '4', '5',
	'6', '7', 0, '8', '9', 0, 0, 0,
	K_F1 + 4, K_F1 + 5, K_F1 + 6, K_F1 + 2,
	K_F1 + 7, K_F1 + 8, 0, K_F1 + 10,
	0, K_F1 + 12, 0, K_F1 + 13,
	0, K_F1 + 9, 0, K_F1 + 11,
	0, K_F1 + 14, K_HELP, K_HOME,
	K_PGUP, K_FDEL, K_F1 + 3, K_END,
	K_F1 + 1, K_PGDN, K_F1, 0,
	0, 0, 0, 0,
};

static void post(int type, int code, int ch)
{
	u32 h = qhead;
	struct event *e;

	if (h - qtail >= QSIZE)
		return;			/* full: drop it */
	e = &queue[h & (QSIZE - 1)];
	e->type = type;
	e->code = code;
	e->ch = ch;
	e->mods = mods;
	e->x = mouse_x;
	e->y = mouse_y;
	e->when = ticks;
	qhead = h + 1;
}

int event_pending(void)
{
	return qtail != qhead;
}

int event_get(struct event *e)
{
	u32 t = qtail;

	if (t == qhead)
		return 0;
	*e = queue[t & (QSIZE - 1)];
	qtail = t + 1;
	return 1;
}

/* Faster hand movements cover more distance per count. */
static int accel(int d)
{
	int a = d < 0 ? -d : d;

	if (a >= 12)
		return d * 4;
	return d * (1 + (a >> 2));
}

void mouse_event(int dx, int dy, int down)
{
	int x = mouse_x + accel(dx);
	int y = mouse_y + accel(dy);

	if (x < 0)
		x = 0;
	if (x >= (int)mach.width)
		x = mach.width - 1;
	if (y < 0)
		y = 0;
	if (y >= (int)mach.height)
		y = mach.height - 1;
	if (x != mouse_x || y != mouse_y) {
		mouse_x = x;
		mouse_y = y;
		cursor_moved();
	}
	if (down != mouse_button) {
		mouse_button = down;
		post(down ? EV_MOUSE_DOWN : EV_MOUSE_UP, 0, 0);
	}
}

static int modifier(int code)
{
	switch (code) {
	case KC_SHIFT: case KC_RSHIFT:		return MOD_SHIFT;
	case KC_CTRL: case KC_RCTRL:		return MOD_CTRL;
	case KC_OPTION: case KC_ROPTION:	return MOD_OPTION;
	case KC_CMD:				return MOD_CMD;
	case KC_CAPS:				return MOD_CAPS;
	}
	return 0;
}

void key_event(u8 code)
{
	int up = code & 0x80, k = code & 0x7f, m, ch;

	if (code == 0x7f || code == 0xff)
		return;			/* power key */
	m = modifier(k);
	if (m) {
		/* caps lock latches in the keyboard: down = on, up = off */
		if (up)
			mods &= ~m;
		else
			mods |= m;
		return;
	}
	if (up) {
		if (k == rep_code)
			rep_code = 0;
		return;
	}
	ch = (u8)((mods & MOD_SHIFT) ? shifted[k] : plain[k]);
	if ((mods & MOD_CAPS) && ch >= 'a' && ch <= 'z')
		ch -= 'a' - 'A';
	if ((mods & MOD_CTRL) && ch >= '@' && ch < 0x80)
		ch &= 0x1f;
	post(EV_KEY, k, ch);
	rep_code = (mods & MOD_CMD) ? 0 : k;
	rep_ch = ch;
	rep_next = ticks + REPEAT_DELAY;
}

/* Called at every vertical blank. */
void input_tick(void)
{
	if (rep_code && (s32)(ticks - rep_next) >= 0) {
		post(EV_KEY, rep_code, rep_ch);
		rep_next = ticks + REPEAT_RATE;
	}
}

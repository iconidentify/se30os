#ifndef INPUT_H
#define INPUT_H

/* Called from the ADB interrupt. */
void mouse_event(int dx, int dy, int button);
void key_event(u8 code);		/* ADB key code; bit 7 set = released */
void input_tick(void);			/* VBL interrupt */

/* Modifier bits */
#define MOD_SHIFT	0x01
#define MOD_CTRL	0x02
#define MOD_OPTION	0x04
#define MOD_CMD		0x08
#define MOD_CAPS	0x10

/* Special keys, delivered as chars above 0x7f */
#define K_UP		0x80
#define K_DOWN		0x81
#define K_LEFT		0x82
#define K_RIGHT		0x83
#define K_HOME		0x84
#define K_END		0x85
#define K_PGUP		0x86
#define K_PGDN		0x87
#define K_FDEL		0x88		/* forward delete */
#define K_HELP		0x89		/* help / insert */
#define K_F1		0x90		/* F1-F15: 0x90-0x9e */

enum { EV_KEY = 1, EV_MOUSE_DOWN, EV_MOUSE_UP };

struct event {
	u8 type;
	u8 code;		/* EV_KEY: ADB key code */
	u8 ch;			/* EV_KEY: character, 0 if none */
	u8 mods;
	s16 x, y;		/* mouse position */
	u32 when;		/* ticks */
};

int event_get(struct event *e);		/* 0 if the queue is empty */
int event_pending(void);
extern volatile int mouse_x, mouse_y, mouse_button;
extern u32 adb_packets, adb_irqs;

/* adb.c */
void adb_init(void);
void adb_irq(void);
void adb_debug(void);

/* cursor.c */
void cursor_init(void);
void cursor_moved(void);		/* interrupt: mouse_x/y changed */
void cursor_shield(int x0, int y0, int x1, int y1);
void cursor_unshield(void);

#endif

/* saver.c - the Omarchy screen savers.
 *
 * The ten effects are the omarchy68k 68k assembly engines written for
 * A/UX on this machine (src/omarchy). As in their A/UX direct demo, each
 * frame the engine updates its grid of cells and raster_diff stamps the
 * changed 6x13 glyphs, white on black, straight into video RAM. Here
 * there is no X server to pause and nothing to restore by hand: when a
 * key, a click or a nudge of the mouse ends it, the window manager
 * repaints the screen from the windows' own bitmaps.
 */
#include "kernel.h"
#include "input.h"
#include "wm.h"
#include "net.h"
#include "saver.h"
#include "omarchy/effects.h"
#include "omarchy/raster.h"
#include "omarchy/demo-font.h"

#define FRAME_BYTES	(64 * 342)
#define HOLD_TICKS	120		/* show each finished banner 2 s */

static DecryptCell cells[STORM_CELLS];
static char previous[STORM_CELLS], text[STORM_CELLS];

/* effects-test.c reports through printf: send it to whoever asked. */
static void (*say)(void *, const char *);
static void *say_arg;

int printf(const char *fmt, ...)
{
	char buf[160];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = kvsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (say)
		say(say_arg, buf);
	kprintf("%s", buf);
	return n;
}

const char *saver_name(int effect)
{
	return effect_name(effect);
}

int saver_parse(const char *name)
{
	return effect_parse(name);
}

int saver_selftest(void (*out)(void *, const char *), void *arg)
{
	int r;

	say = out;
	say_arg = arg;
	r = effects_selftest();
	say = NULL;
	return r;
}

static int new_scene(int effect, u32 seed)
{
	int rows = effect_reset(effect, cells, text, seed);

	memset(previous, 0, sizeof(previous));
	memset(mach.video, 0xff, FRAME_BYTES);	/* black */
	return rows;
}

/* Anything that should wake us: a key, a click, or the mouse moving. */
static int woken(int mx, int my)
{
	struct event e;
	int dx = mouse_x - mx, dy = mouse_y - my;

	while (event_get(&e))
		if (e.type == EV_KEY || e.type == EV_MOUSE_DOWN)
			return 1;
	return mouse_button || dx > 3 || dx < -3 || dy > 3 || dy < -3;
}

int saver_run(int choice, int seconds, int fps, struct saver_stats *st)
{
	int effect, rows, done = 0, mx = mouse_x, my = mouse_y;
	u32 seed, start, frame_at, finished = 0, step;
	struct event e;

	/* the engines and raster_diff know only the SE/30's own screen */
	if (mach.width != 512 || mach.height != 342 || mach.rowbytes != 64 ||
	    mach.depth != 1)
		return -1;
	if (choice < EFFECT_CYCLE || choice >= EFFECT_COUNT)
		return -1;
	step = fps > 0 && fps < 60 ? 60 / fps : 1;
	memset(st, 0, sizeof(*st));

	cursor_shield(0, 0, mach.width, mach.height);	/* no pointer */
	seed = ticks * 2654435761u + 1;
	effect = choice == EFFECT_CYCLE ? (int)(seed >> 8) % EFFECT_COUNT : choice;
	rows = new_scene(effect, seed);
	start = frame_at = ticks;
	while (!woken(mx, my)) {
		if (seconds && ticks - start >= (u32)seconds * 60)
			break;
		net_poll();		/* keep connections alive meanwhile */
		if (!done) {
			done = effect_tick(effect, cells, text);
			st->frames++;
			st->glyphs += raster_diff(cells, previous, mach.video,
						  demo_font, rows, 13,
						  (342 - rows * 13) / 2);
			if (done) {
				st->reveals++;
				finished = ticks;
			}
		} else if (ticks - finished >= HOLD_TICKS) {
			if (choice == EFFECT_CYCLE)
				effect = (effect + 1) % EFFECT_COUNT;
			seed = seed * 1664525u + 1013904223u;
			rows = new_scene(effect, seed);
			done = 0;
		}
		/* pace to the frame rate; finished banners just wait */
		while (ticks - frame_at < step)
			cpu_idle();
		frame_at = ticks;
	}
	st->ticks = ticks - start;
	while (event_get(&e))		/* the wake-up isn't for anyone else */
		;
	wm_redraw();
	cursor_unshield();
	return 0;
}

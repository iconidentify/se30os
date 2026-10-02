#ifndef SAVER_H
#define SAVER_H

#define SAVER_CYCLE	(-1)		/* all ten effects in turn */

struct saver_stats {
	u32 frames, glyphs, reveals, ticks;
};

/* Run an effect (or SAVER_CYCLE) full screen until a key, a click or
 * the mouse moves, or for seconds if non-zero. -1 if it can't run. */
int saver_run(int effect, int seconds, int fps, struct saver_stats *st);
int saver_parse(const char *name);		/* -2 if unknown */
const char *saver_name(int effect);
int saver_selftest(void (*out)(void *, const char *), void *arg);

#endif

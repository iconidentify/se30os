/* Scene layout/selection only; animation is in decrypt.s, storm.s, extras.s, and motion.s. */
#include <string.h>
#include "effects.h"
#include "banner.h"
static const char *names[EFFECT_COUNT] = {
    "decrypt", "thunderstorm", "matrix", "spotlights", "laseretch", "vhstape",
    "fireworks", "bouncyballs", "expand", "beams"
};
const char *effect_name(int effect)
{
    return effect == EFFECT_CYCLE ? "cycle" :
           effect >= 0 && effect < EFFECT_COUNT ? names[effect] : "invalid";
}
int effect_parse(const char *name)
{
    int i;
    if (!strcmp(name, "cycle")) return EFFECT_CYCLE;
    for (i = 0; i < EFFECT_COUNT; i++) if (!strcmp(name, names[i])) return i;
    return -2;
}
int effect_reset(int effect, DecryptCell *cells, char *text, unsigned long seed)
{
    int r, top;
    if (!effect) {
        decrypt_reset(cells, banner, BANNER_ROWS * BANNER_COLS, seed);
        return BANNER_ROWS;
    }
    memset(text, ' ', STORM_CELLS);
    /* Leave room for rain and moving beams, while relocating each reveal. */
    top = 4 + (int)((seed >> 16) % 7);
    for (r = 0; r < BANNER_ROWS; r++)
        memcpy(text + (r + top) * BANNER_COLS, banner + r * BANNER_COLS, BANNER_COLS);
    if (effect == 1) storm_reset(cells, text, STORM_CELLS, seed);
    else if (effect < 6) extra_reset(cells, text, STORM_CELLS, seed, effect);
    else motion_reset(cells, text, STORM_CELLS, seed, effect);
    return STORM_ROWS;
}
int effect_tick(int effect, DecryptCell *cells, const char *text)
{
    if (!effect) return decrypt_tick(cells, banner, BANNER_ROWS * BANNER_COLS);
    if (effect == 1) return storm_tick(cells, text, STORM_CELLS);
    return effect < 6 ? extra_tick(cells, text, STORM_CELLS) :
                        motion_tick(cells, text, STORM_CELLS);
}

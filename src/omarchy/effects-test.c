/* Native tests execute the assembled engines, without X or video mapping. */
#include <stdio.h>
#include <string.h>
#include "effects.h"
#include "banner.h"
static struct {
    unsigned long before;
    DecryptCell cells[STORM_CELLS];
    unsigned long after;
} guarded;
static struct {
    unsigned long before;
    char data[STORM_CELLS];
    unsigned long after;
} input;
static unsigned long hashes[2049];
static unsigned char frozen[81];
static unsigned long hash(void)
{
    int i; unsigned long h = 2166136261UL;
    for (i = 0; i < STORM_CELLS; i++)
        h = (h ^ guarded.cells[i].symbol) * 16777619UL;
    return h;
}
static void reset_engine(int effect, DecryptCell *cells, const char *text,
                         int count, unsigned long seed, int kind)
{
    if (effect < 6) extra_reset(cells, text, count, seed, kind);
    else motion_reset(cells, text, count, seed, kind);
}
static int tick_engine(int effect, DecryptCell *cells, const char *text, int count)
{
    return effect < 6 ? extra_tick(cells, text, count) : motion_tick(cells, text, count);
}
int effects_selftest(void)
{
    int effect, seed, pass, frame, done, i, col, row, failures = 0;
    int completion = 0, saw_hidden, saw_visible, saw_change, settled, last_settled;
    unsigned long original, prior;
    char *text = input.data;
    for (effect = 2; effect < EFFECT_COUNT; effect++) {
        int before = failures;
        for (seed = 0; seed < 8; seed++) {
            for (pass = 0; pass < 2; pass++) {
                memset(text, ' ', STORM_CELLS);
                memcpy(text + 7 * 81, banner, BANNER_ROWS * BANNER_COLS);
                input.before = 0x43211234UL; input.after = 0xabcd0123UL;
                guarded.before = 0x12345678UL; guarded.after = 0x87654321UL;
                original = 0;
                for (i = 0; i < STORM_CELLS; i++) original += (unsigned char)text[i];
                reset_engine(effect, guarded.cells, text, STORM_CELLS, seed, effect);
                memset(frozen, 0, sizeof frozen);
                prior = hash(); saw_hidden = saw_visible = saw_change = last_settled = 0;
                done = 0;
                for (frame = 1; frame <= 2048 && !done; frame++) {
                    done = tick_engine(effect, guarded.cells, text, STORM_CELLS);
                    if (pass == 0) hashes[frame] = hash();
                    else if (hashes[frame] != hash()) failures++;
                    if (hash() != prior) saw_change = 1;
                    prior = hash(); settled = 0;
                    for (i = 0; i < STORM_CELLS; i++) {
                        unsigned char c = guarded.cells[i].symbol;
                        if (c < 32 || c > 126) failures++;
                        if (effect == 3 && c != ' ' && c != (unsigned char)text[i]) failures++;
                        if (effect == 3 && text[i] != ' ') {
                            if (c == ' ') saw_hidden = 1; else saw_visible = 1;
                        }
                        if (guarded.cells[i].phase) settled++;
                    }
                    if ((effect == 4 || effect == 6 || effect == 7 || effect == 9) && settled < last_settled) failures++;
                    last_settled = settled;
                    if (effect == 2 && frame >= 241 && frame <= 321) {
                        col = ((frame - 241) * 31) % 81;
                        if (frozen[col]) failures++;
                        frozen[col] = 1;
                        for (col = 0; col < 81; col++) if (frozen[col])
                            for (row = 0; row < 26; row++)
                                if (guarded.cells[row * 81 + col].symbol != text[row * 81 + col])
                                    failures++;
                    }
                    if (guarded.before != 0x12345678UL || guarded.after != 0x87654321UL ||
                        input.before != 0x43211234UL || input.after != 0xabcd0123UL) failures++;
                }
                if (!done || !saw_change || (effect == 3 && (!saw_hidden || !saw_visible))) failures++;
                if (!pass) completion = frame;
                else if (completion != frame) failures++;
                prior = 0;
                for (i = 0; i < STORM_CELLS; i++) {
                    if (guarded.cells[i].symbol != (unsigned char)text[i]) failures++;
                    prior += (unsigned char)text[i];
                }
                if (prior != original) failures++;
                if (!tick_engine(effect, guarded.cells, text, STORM_CELLS)) failures++;
                for (i = 0; i < STORM_CELLS; i++)
                    if (guarded.cells[i].symbol != (unsigned char)text[i]) failures++;
            }
        }
        /* Empty input, reinitialization, invalid count and invalid mode. */
        memset(text, ' ', STORM_CELLS);
        reset_engine(effect, guarded.cells, text, STORM_CELLS, 0, effect);
        for (frame = 0; frame < 2048; frame++)
            if (tick_engine(effect, guarded.cells, text, STORM_CELLS)) break;
        if (frame == 2048) failures++;
        for (i = 0; i < STORM_CELLS; i++) if (guarded.cells[i].symbol != ' ') failures++;
        prior = hash();
        reset_engine(effect, guarded.cells, text, -1, 1, effect);
        reset_engine(effect, guarded.cells, text, STORM_CELLS + 1, 1, effect);
        reset_engine(effect, guarded.cells, text, STORM_CELLS, 1, 99);
        if (!tick_engine(effect, guarded.cells, text, -1) ||
            !tick_engine(effect, guarded.cells, text, STORM_CELLS + 1) || prior != hash()) failures++;
        /* Full canvas and edge-only input exercise pool reuse and clipping. */
        if (effect >= 6) {
            int pattern;
            for (pattern = 0; pattern < 2; pattern++) {
                memset(text, pattern ? ' ' : '#', STORM_CELLS);
                text[0] = 'A'; text[80] = 'B'; text[2025] = 'C'; text[2105] = 'D';
                reset_engine(effect, guarded.cells, text, STORM_CELLS, 17, effect);
                done = 0;
                for (frame = 0; frame < 2048 && !done; frame++) {
                    done = tick_engine(effect, guarded.cells, text, STORM_CELLS);
                    if (guarded.before != 0x12345678UL || guarded.after != 0x87654321UL)
                        failures++;
                }
                if (!done) failures++;
                for (i = 0; i < STORM_CELLS; i++)
                    if (guarded.cells[i].symbol != (unsigned char)text[i]) failures++;
            }
        }
        printf("68k %s: 8 seeds, determinism, bounds, phases, completion: %d failures\n",
               effect_name(effect), failures - before);
    }
    return failures != 0;
}

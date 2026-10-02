#ifndef OMARCHY_STORM_H
#define OMARCHY_STORM_H
#include "decrypt.h"

/* Fixed 81-column display grid; no allocation during an animation. */
#define STORM_COLS 81
#define STORM_ROWS 26
#define STORM_CELLS (STORM_COLS * STORM_ROWS)
void storm_reset(DecryptCell *, const char *, int, unsigned long);
int storm_tick(DecryptCell *, const char *, int);

#endif

#ifndef OMARCHY_RASTER_H
#define OMARCHY_RASTER_H
#include "decrypt.h"
/* 512x342, MSB-first, 1=black; 81 columns, fixed 6x13 font.
 * Glyph slots are 16 bytes each: 13 rows of six high white-pixel bits.
 * Previous holds rows*81 chars. Returns the number of changed cells.
 */
int raster_diff(const DecryptCell *, char *, unsigned char *,
                const unsigned char *, int rows, int x, int y);
#endif

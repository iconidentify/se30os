#ifndef OMARCHY_DECRYPT_H
#define OMARCHY_DECRYPT_H

/* A/UX gcc: short=2, long/pointer=4. Keep this exact eight-byte layout. */
typedef struct {
    unsigned short phase, ticks, units;
    unsigned char symbol, pad;
} DecryptCell;

void decrypt_reset(DecryptCell *, const char *, int, unsigned long);
int decrypt_tick(DecryptCell *, const char *, int);

#endif

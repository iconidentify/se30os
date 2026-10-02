#ifndef OMARCHY_EFFECTS_H
#define OMARCHY_EFFECTS_H
#include "storm.h"
#define EFFECT_COUNT 10
#define EFFECT_CYCLE (-1)
const char *effect_name(int);
int effect_parse(const char *);
int effect_reset(int, DecryptCell *, char *, unsigned long);
int effect_tick(int, DecryptCell *, const char *);
int effects_selftest(void);
void extra_reset(DecryptCell *, const char *, int, unsigned long, int);
int extra_tick(DecryptCell *, const char *, int);
void motion_reset(DecryptCell *, const char *, int, unsigned long, int);
int motion_tick(DecryptCell *, const char *, int);
#endif

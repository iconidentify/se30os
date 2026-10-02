#ifndef REGION_H
#define REGION_H

#include "gfx.h"

/* A region: a set of non-overlapping rectangles. */
#define REGION_MAX 96

struct region {
	int n;
	struct rect r[REGION_MAX];
};

void region_set(struct region *g, const struct rect *r);
void region_add(struct region *g, const struct rect *r);	/* r disjoint */
void region_sub(struct region *g, const struct rect *b);

#endif

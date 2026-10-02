/* region.c - regions as lists of disjoint rectangles. */
#include "kernel.h"
#include "region.h"

void region_set(struct region *g, const struct rect *r)
{
	g->n = 0;
	region_add(g, r);
}

void region_add(struct region *g, const struct rect *r)
{
	if (!rect_empty(r) && g->n < REGION_MAX)
		g->r[g->n++] = *r;
}

/* Remove b: each rectangle it touches breaks into up to four pieces
 * (above, below, left and right of b). */
void region_sub(struct region *g, const struct rect *b)
{
	struct rect in[REGION_MAX];
	int n = g->n, i;

	memcpy(in, g->r, n * sizeof(struct rect));
	g->n = 0;
	for (i = 0; i < n; i++) {
		struct rect a = in[i], s, p;

		if (!rect_sect(&a, b, &s)) {
			region_add(g, &a);
			continue;
		}
		p = mkrect(a.left, a.top, a.right, s.top);
		region_add(g, &p);
		p = mkrect(a.left, s.bottom, a.right, a.bottom);
		region_add(g, &p);
		p = mkrect(a.left, s.top, s.left, s.bottom);
		region_add(g, &p);
		p = mkrect(s.right, s.top, a.right, s.bottom);
		region_add(g, &p);
	}
}

/* mem.c - the kernel heap: first fit over a free list kept in address
 * order, so that freeing a block can merge it with its neighbours. */
#include "kernel.h"

struct block {
	u32 size;			/* including this header */
	struct block *next;		/* next free block (free blocks only) */
};

#define HDR	8
#define ALIGN	16

static struct block *free_list;
static u32 heap_start, heap_end, heap_free;

extern char _end[];

/* Make the free list from the RAM Penguin reported. Penguin sorts the
 * chunks largest first, and the largest need not be the one at address 0
 * where the kernel lives, so every chunk counts, less the kernel's part. */
void mem_init(void)
{
	u32 kend = ((u32)_end + 4096 + ALIGN - 1) & ~(ALIGN - 1);
	int i;

	free_list = NULL;
	heap_free = 0;
	for (i = 0; i < mach.nchunks; i++) {
		u32 start = mach.chunk[i].addr, end = start + mach.chunk[i].size;

		/* the kernel and Penguin's bootinfo just past it */
		if (start < kend && end > 0x1000)
			start = kend;
		start = (start + ALIGN - 1) & ~(ALIGN - 1);
		if (end <= start || end - start < 4096)
			continue;
		{
			/* keep the list in address order, for kfree's merging */
			struct block *b = (struct block *)start, **pp = &free_list;

			while (*pp && (u32)*pp < start)
				pp = &(*pp)->next;
			b->size = end - start;
			b->next = *pp;
			*pp = b;
			heap_free += end - start;
		}
		if (!heap_start || start < heap_start)
			heap_start = start;
		if (end > heap_end)
			heap_end = end;
	}
	if (!free_list) {		/* no chunks: trust mem_base/size */
		free_list = (struct block *)kend;
		free_list->size = mach.mem_base + mach.mem_size - kend;
		free_list->next = NULL;
		heap_free = free_list->size;
	}
}

void *kmalloc(u32 n)
{
	struct block **pp, *b;
	u16 sr;

	n = (n + HDR + ALIGN - 1) & ~(ALIGN - 1);
	sr = irq_save();
	for (pp = &free_list; (b = *pp); pp = &b->next) {
		if (b->size < n)
			continue;
		if (b->size - n >= 2 * ALIGN) {
			/* split: the tail stays free */
			struct block *rest = (struct block *)((u8 *)b + n);

			rest->size = b->size - n;
			rest->next = b->next;
			*pp = rest;
			b->size = n;
		} else {
			*pp = b->next;
		}
		heap_free -= b->size;
		irq_restore(sr);
		return (u8 *)b + HDR;
	}
	irq_restore(sr);
	return NULL;
}

void *kzalloc(u32 n)
{
	u32 *p = kmalloc(n), *q = p;

	if (p) {
		n = (n + 3) / 4;
		while (n--)
			*q++ = 0;
	}
	return p;
}

void kfree(void *p)
{
	struct block *b, **pp, *prev = NULL;
	u16 sr;

	if (!p)
		return;
	b = (struct block *)((u8 *)p - HDR);
	sr = irq_save();
	heap_free += b->size;
	for (pp = &free_list; *pp && *pp < b; pp = &(*pp)->next)
		prev = *pp;
	b->next = *pp;
	*pp = b;
	if (b->next && (u8 *)b + b->size == (u8 *)b->next) {
		b->size += b->next->size;
		b->next = b->next->next;
	}
	if (prev && (u8 *)prev + prev->size == (u8 *)b) {
		prev->size += b->size;
		prev->next = b->next;
	}
	irq_restore(sr);
}

u32 mem_free(void)
{
	return heap_free;
}

/* mmu.c - an identity map, so that the data cache can be turned on.
 *
 * With the MMU off the 68030 caches every access, which would make it read
 * stale VIA and SCSI registers. A 16-entry root table of early-terminating
 * page descriptors maps each 256 MB block onto itself: RAM and ROM
 * cacheable, I/O and NuBus/PDS space (which holds the framebuffer) not.
 */
#include "kernel.h"

#define DT_PAGE		0x01	/* page descriptor (early termination) */
#define PD_U		0x08	/* used: preset so the MMU never writes back */
#define PD_M		0x10	/* modified */
#define PD_CI		0x40	/* cache inhibit */

/* TC: enable, 32K pages, IS 0, TIA 4, TIB 13 (4 + 13 + 15 = 32 bits) */
#define TC_VALUE	0x80f04d00

static u32 root[16] __attribute__((aligned(16)));
static u32 crp[2];
static u32 tc, zero;

void mmu_init(void)
{
	int i;

	for (i = 0; i < 16; i++) {
		root[i] = (u32)i << 28 | PD_M | PD_U | DT_PAGE;
		if (i >= 5)		/* 0x50000000 up: I/O, slots, video */
			root[i] |= PD_CI;
	}
	/* Upper limit 0x7fff, i.e. none, and 4-byte descriptors. (0x00000002
	   would be an upper limit of 0: a real 68030 then faults on every
	   address above 256 MB, the screen included, which hung the SE/30
	   right after "mmu..."; MAME doesn't check limits.) */
	crp[0] = 0x7fff0002;
#ifdef TEST_MMU_LIMIT
	crp[0] = 0x00000002;		/* the old bug, to test crash reports */
#endif
	crp[1] = (u32)root;
	tc = TC_VALUE;
	__asm__ volatile(
		"pmove	(%0),%%tt0\n\t"	/* Mac OS may have left these on */
		"pmove	(%0),%%tt1\n\t"
		"pflusha\n\t"
		"pmove	(%1),%%crp\n\t"
		"pmove	(%2),%%tc\n\t"
		:: "a"(&zero), "a"(crp), "a"(&tc) : "memory");
}

/* Instruction and data caches on (cleared first), with burst fills and
 * write-allocate. */
void cache_on(void)
{
	__asm__ volatile("movec %0,%%cacr" :: "d"(0x3919) : "memory");
}

/* MMU off, for reporting a crash: the map is one to one, so nothing moves */
void mmu_off(void)
{
	__asm__ volatile("pmove (%0),%%tc" :: "a"(&zero) : "memory");
}

void cache_off(void)
{
	__asm__ volatile("movec %0,%%cacr" :: "d"(0x0808) : "memory");
}

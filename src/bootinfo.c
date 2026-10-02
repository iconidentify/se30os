/* bootinfo.c - read the records Penguin leaves after the kernel image. */
#include "kernel.h"

/* Record tags (from the Linux/m68k bootinfo interface) */
#define BI_LAST		0x0000
#define BI_CPUTYPE	0x0002
#define BI_FPUTYPE	0x0003
#define BI_MMUTYPE	0x0004
#define BI_MEMCHUNK	0x0005
#define BI_COMMAND_LINE	0x0007
#define BI_MAC_MODEL	0x8000
#define BI_MAC_VADDR	0x8001
#define BI_MAC_VDEPTH	0x8002
#define BI_MAC_VROW	0x8003
#define BI_MAC_VDIM	0x8004
#define BI_MAC_SCCBASE	0x8006
#define BI_MAC_ROMBASE	0x800b
#define BI_MAC_VIA1BASE	0x8010
#define BI_MAC_VIA2BASE	0x8011

struct bi_record {
	u16 tag;
	u16 size;		/* including this header */
	u32 data[];
};

struct machine mach;

int bootinfo_version;		/* 1: Penguin's old flat struct, 2: records */

/* Bootinfo version 1: one flat structure (Penguin falls back to it when
 * it can't read the kernel's version table). All fields are longs. */
static void parse_v1(const u32 *b)
{
	int i, n = b[10];

	mach.cpu = b[1];
	for (i = 0; i < n && i < 4 && i < 8; i++) {
		mach.chunk[i].addr = b[2 + i * 2];
		mach.chunk[i].size = b[3 + i * 2];
	}
	mach.nchunks = i;
	if (i) {
		mach.mem_base = mach.chunk[0].addr;
		mach.mem_size = mach.chunk[0].size;
	}
	for (i = 0; i < (int)sizeof(mach.cmdline) - 1; i++) {
		mach.cmdline[i] = ((const char *)b)[52 + i];
		if (!mach.cmdline[i])
			break;
	}
	mach.cmdline[i] = 0;
	b = (const u32 *)((const u8 *)b + 308);		/* the Mac part */
	mach.video = (u8 *)b[0];
	mach.rowbytes = b[1];
	mach.depth = b[2];
	mach.width = b[3] & 0xffff;
	mach.height = b[3] >> 16;
	mach.scc = (volatile u8 *)b[9];
	mach.model = b[10];
	mach.rombase = 0x40800000;
}

void bootinfo_parse(const void *records)
{
	const struct bi_record *r = records;
	const u32 *first = records;

	if (first[0] == 3) {			/* machtype MACH_MAC, not a tag */
		bootinfo_version = 1;
		parse_v1(records);
		goto fixups;
	}
	bootinfo_version = 2;

	while (r->tag != BI_LAST) {
		switch (r->tag) {
		case BI_CPUTYPE:	mach.cpu = r->data[0]; break;
		case BI_FPUTYPE:	mach.fpu = r->data[0]; break;
		case BI_MMUTYPE:	mach.mmu = r->data[0]; break;
		case BI_MEMCHUNK:
			if (!mach.mem_size) {
				mach.mem_base = r->data[0];
				mach.mem_size = r->data[1];
			}
			if (mach.nchunks < 8) {
				mach.chunk[mach.nchunks].addr = r->data[0];
				mach.chunk[mach.nchunks].size = r->data[1];
				mach.nchunks++;
			}
			break;
		case BI_COMMAND_LINE: {
			const char *c = (const char *)r->data;
			int i;

			for (i = 0; i < (int)sizeof(mach.cmdline) - 1 && c[i] &&
				    i < r->size - 4; i++)
				mach.cmdline[i] = c[i];
			mach.cmdline[i] = 0;
			break;
		}
		case BI_MAC_MODEL:	mach.model = r->data[0]; break;
		case BI_MAC_VADDR:	mach.video = (u8 *)r->data[0]; break;
		case BI_MAC_VDEPTH:	mach.depth = r->data[0]; break;
		case BI_MAC_VROW:	mach.rowbytes = r->data[0]; break;
		case BI_MAC_VDIM:
			mach.width = r->data[0] & 0xffff;
			mach.height = r->data[0] >> 16;
			break;
		case BI_MAC_SCCBASE:	mach.scc = (volatile u8 *)r->data[0]; break;
		case BI_MAC_ROMBASE:	mach.rombase = r->data[0]; break;
		case BI_MAC_VIA1BASE:	mach.via1 = (volatile u8 *)r->data[0]; break;
		case BI_MAC_VIA2BASE:	mach.via2 = (volatile u8 *)r->data[0]; break;
		}
		if (r->size < 4)
			break;			/* garbage: don't loop forever */
		r = (const struct bi_record *)((const u8 *)r + r->size);
	}
fixups:
	/* Older Penguins don't pass the VIA addresses, and an address
	   outside the I/O space can't be right: use the SE/30's (and the
	   whole Mac II family's) in 32-bit mode. */
	if (((u32)mach.via1 >> 24) != 0x50)
		mach.via1 = (volatile u8 *)0x50f00000;
	if (((u32)mach.via2 >> 24) != 0x50)
		mach.via2 = (volatile u8 *)0x50f02000;
	/* The SCC address may come in 24-bit form; Linux fixes it the same
	   way. */
	if (((u32)mach.scc >> 24) == 0)
		mach.scc = (volatile u8 *)((u32)mach.scc | 0x50000000);
	if (((u32)mach.scc >> 24) != 0x50)
		mach.scc = (volatile u8 *)0x50f04000;
}

/* Penguin's command line, shared with Linux, can carry se30os options:
   nommu, nocache, nonet, noadb, wait. */
int boot_option(const char *word)
{
	const char *c = mach.cmdline;
	int n = 0;

	while (word[n])
		n++;
	while (*c) {
		int i = 0;

		while (*c == ' ')
			c++;
		while (i < n && c[i] == word[i])
			i++;
		if (i == n && (c[n] == ' ' || c[n] == 0))
			return 1;
		while (*c && *c != ' ')
			c++;
	}
	return 0;
}

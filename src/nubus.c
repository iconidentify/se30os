/* nubus.c - find cards in the NuBus and PDS slots by reading their
 * declaration ROMs.
 *
 * A card's ROM sits at the very top of its slot's 16 MB. The last byte
 * says which of the four byte lanes the ROM answers on; ROM offsets
 * count only bytes in those lanes. Just below it is the format block,
 * which points at the sResource directory: a board sResource and one
 * functional sResource per thing the card does, each a directory of
 * (id, 24-bit offset or data) entries.
 */
#include "kernel.h"
#include "nubus.h"

int probe8(volatile u8 *addr, u8 *out);

#define TEST_PATTERN	0x5a932bc7
#define RES_TYPE	0x01
#define RES_NAME	0x02

static int useful(u32 p, u8 lanes)
{
	return lanes & (1 << (p & 3));
}

/* Move n ROM bytes along (back if n < 0), landing on a ROM byte. */
static u32 rom_move(u32 p, s32 n, u8 lanes)
{
	for (; n > 0; n--)
		do
			p++;
		while (!useful(p, lanes));
	for (; n < 0; n++)
		do
			p--;
		while (!useful(p, lanes));
	return p;
}

/* Read n ROM bytes big-endian from *pp and move past them. */
static u32 rom_get(u32 *pp, int n, u8 lanes)
{
	u32 v = 0, p = *pp;

	while (n--) {
		v = v << 8 | *(volatile u8 *)p;
		p = rom_move(p, 1, lanes);
	}
	*pp = p;
	return v;
}

static s32 sext24(u32 v)
{
	return v & 0x800000 ? (s32)(v | 0xff000000) : (s32)v;
}

struct dirent {
	u8 id;
	u32 data;			/* 24-bit offset or value */
	u32 at;				/* where the entry is */
};

static int dir_next(u32 *pp, u8 lanes, struct dirent *e)
{
	u32 v;

	e->at = *pp;
	v = rom_get(pp, 4, lanes);
	if ((v >> 24) == 0xff)
		return 0;
	e->id = v >> 24;
	e->data = v & 0xffffff;
	return 1;
}

static u32 dir_ptr(const struct dirent *e, u8 lanes)
{
	return rom_move(e->at, sext24(e->data), lanes);
}

static int dir_find(u32 dir, u8 lanes, u8 id, struct dirent *e)
{
	u32 p = dir;
	int n = 0;

	while (dir_next(&p, lanes, e) && n++ < 256)
		if (e->id == id)
			return 1;
	return 0;
}

/* The slot's root directory, or 0 if there is no valid ROM there. */
static u32 slot_root(int slot, u8 *lanes_out)
{
	u32 top = NUBUS_SLOT_ADDR(slot) + 0x1000000, p, fb;
	u8 lanes = 0, b;
	s32 doff;
	int i;

	/* the last byte in a lane is the ByteLanes value: the low nibble
	   the lanes, the high nibble its complement */
	for (i = 4, p = top; i; i--) {
		p--;
		if (!probe8((volatile u8 *)p, &b))
			continue;
		if ((((b >> 4) ^ b) & 0x0f) != 0x0f)
			continue;
		if ((b & 0x0f) >= (1 << i))
			continue;
		lanes = b & 0x0f;
		break;
	}
	if (!lanes)
		return 0;
	fb = rom_move(top, -20, lanes);		/* the format block */
	p = fb;
	doff = sext24(rom_get(&p, 4, lanes) & 0xffffff);
	rom_get(&p, 4, lanes);			/* ROM length */
	rom_get(&p, 4, lanes);			/* CRC */
	rom_get(&p, 2, lanes);			/* revision, format */
	if (rom_get(&p, 4, lanes) != TEST_PATTERN)
		return 0;
	*lanes_out = lanes;
	return rom_move(fb, doff, lanes);
}

static void read_func(int slot, u8 lanes, u32 dir, struct nubus_func *f)
{
	struct dirent e;
	u32 p;
	int i;

	f->slot = slot;
	f->lanes = lanes;
	f->dir = dir;
	f->category = f->type = f->drsw = f->drhw = 0;
	f->name[0] = 0;
	if (dir_find(dir, lanes, RES_TYPE, &e)) {
		p = dir_ptr(&e, lanes);
		f->category = rom_get(&p, 2, lanes);
		f->type = rom_get(&p, 2, lanes);
		f->drsw = rom_get(&p, 2, lanes);
		f->drhw = rom_get(&p, 2, lanes);
	}
	if (dir_find(dir, lanes, RES_NAME, &e)) {
		p = dir_ptr(&e, lanes);
		for (i = 0; i < (int)sizeof(f->name) - 1; i++) {
			char c = rom_get(&p, 1, lanes);

			if (!c)
				break;
			f->name[i] = c;
		}
		f->name[i] = 0;
	}
}

/* Calls fn for every functional sResource of every card. */
static int each_func(int (*fn)(struct nubus_func *, void *), void *arg)
{
	struct nubus_func f;
	struct dirent e;
	int slot;

	for (slot = 0x9; slot <= 0xe; slot++) {
		u8 lanes;
		u32 root = slot_root(slot, &lanes), p = root;
		int n = 0;

		if (!root)
			continue;
		while (dir_next(&p, lanes, &e) && n++ < 64) {
			read_func(slot, lanes, dir_ptr(&e, lanes), &f);
			if (fn(&f, arg))
				return 1;
		}
	}
	return 0;
}

struct find {
	u16 category, type;
	struct nubus_func *out;
};

static int find_fn(struct nubus_func *f, void *arg)
{
	struct find *q = arg;

	if (f->category != q->category || f->type != q->type)
		return 0;
	*q->out = *f;
	return 1;
}

int nubus_find(u16 category, u16 type, struct nubus_func *f)
{
	struct find q = { category, type, f };

	return each_func(find_fn, &q);
}

/* Copy len bytes of the resource with this id. 4-byte values that fit in
   the entry itself (len <= 3) come from the entry. */
int nubus_get(const struct nubus_func *f, u8 id, void *buf, int len)
{
	struct dirent e;
	u32 p;
	u8 *b = buf;

	if (!dir_find(f->dir, f->lanes, id, &e))
		return 0;
	p = dir_ptr(&e, f->lanes);
	while (len--)
		*b++ = rom_get(&p, 1, f->lanes);
	return 1;
}

struct lister {
	void (*print)(void *, const char *, ...);
	void *arg;
};

static int list_fn(struct nubus_func *f, void *arg)
{
	struct lister *l = arg;

	l->print(l->arg, "slot %x: %s (category %x type %x sw %x hw %x)\n",
		 f->slot, f->name[0] ? f->name : "?", f->category, f->type,
		 f->drsw, f->drhw);
	return 0;
}

void nubus_list(void (*print)(void *, const char *, ...), void *arg)
{
	struct lister l = { print, arg };

	each_func(list_fn, &l);
}

#ifndef NUBUS_H
#define NUBUS_H

/* A functional sResource on a card: one thing the card does. */
struct nubus_func {
	int slot;
	u8 lanes;			/* byte lanes the ROM answers on */
	u32 dir;			/* the sResource's directory */
	u16 category, type, drsw, drhw;
	char name[32];
};

#define NUBUS_CAT_NETWORK	0x0004
#define NUBUS_TYPE_ETHERNET	0x0001
#define NUBUS_RES_MINOR_BASE	0x0a
#define NUBUS_RES_MINOR_LENGTH	0x0b
#define NUBUS_RES_MAC_ADDRESS	0x80

/* Slot space: 0xFs000000; cards built for 24-bit Macs answer at Fss00000. */
#define NUBUS_SLOT_ADDR(s)	(0xf0000000u | (u32)(s) << 24)

int nubus_find(u16 category, u16 type, struct nubus_func *f);
int nubus_get(const struct nubus_func *f, u8 id, void *buf, int len);
void nubus_list(void (*print)(void *, const char *, ...), void *arg);

#endif

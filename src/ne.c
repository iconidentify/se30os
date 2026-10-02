/* ne.c - Ethernet cards built around National's DP8390: the Asante
 * MacCon and Farallon EtherMac PDS cards for the SE/30, and Apple's
 * NuBus Ethernet. The chip receives into, and transmits from, RAM on the
 * card, which the CPU reads and writes directly; no DMA crosses the bus.
 *
 * The card RAM starts at the declaration ROM's MinorBaseOS offset and
 * the chip's registers sit 64 KB above it, in reverse order a long
 * apart. Card RAM is only ever touched 16 bits at a time: some Asante
 * cards corrupt memory when accessed 32 bits wide.
 */
#include "kernel.h"
#include "nubus.h"
#include "net.h"

/* Page 0 registers */
#define CR	0x00
#define PSTART	0x01
#define PSTOP	0x02
#define BNRY	0x03
#define TPSR	0x04		/* write */
#define TSR	0x04		/* read */
#define TBCR0	0x05
#define TBCR1	0x06
#define ISR	0x07
#define RBCR0	0x0a
#define RBCR1	0x0b
#define RCR	0x0c		/* write */
#define RSR	0x0c		/* read */
#define TCR	0x0d
#define DCR	0x0e
#define IMR	0x0f
/* Page 1 */
#define PAR0	0x01
#define CURR	0x07
#define MAR0	0x08

#define CR_STOP		0x01
#define CR_START	0x02
#define CR_TXP		0x04
#define CR_NODMA	0x20
#define CR_PAGE1	0x40

#define ISR_PRX		0x01
#define ISR_PTX		0x02
#define ISR_RXE		0x04
#define ISR_TXE		0x08
#define ISR_OVW		0x10
#define ISR_RST		0x80

#define TX_PAGES	6		/* one full frame */
#define RX_START	TX_PAGES

static volatile u8 *regs;
static volatile u16 *mem;		/* card RAM */
static int rx_stop;			/* first page past the receive ring */
static int slot;

struct ne_stats ne_stats;

/* The receive queue between the interrupt handler and net_poll() */
#define RXQ	32
static struct {
	u16 len;
	u8 data[1536];
} rxq[RXQ];
static volatile u32 rxq_head, rxq_tail;

static inline void wr(int r, u8 v)
{
	regs[(15 - r) * 4] = v;
}

static inline u8 rd(int r)
{
	return regs[(15 - r) * 4];
}

static void delay(int n)
{
	volatile int i;

	for (i = 0; i < n; i++)
		;
}

/* Copy len bytes out of card RAM, from byte offset off (even). */
static void card_read(u8 *dst, u32 off, int len)
{
	volatile u16 *s = mem + off / 2;
	u16 *d = (u16 *)dst;		/* dst is long aligned */

	for (; len > 1; len -= 2)
		*d++ = *s++;
	if (len) {
		u16 w = *s;

		*(u8 *)d = w >> 8;
	}
}

static void card_write(u32 off, const u8 *src, int len)
{
	volatile u16 *d = mem + off / 2;

	for (; len > 1; len -= 2, src += 2)
		*d++ = src[0] << 8 | src[1];
	if (len)
		*d = src[0] << 8;
}

/* Take every complete frame out of the ring into the receive queue. */
static void drain_ring(void)
{
	int guard = 64;

	while (guard--) {
		u8 curr, next;
		u16 hdr[2];
		int len, page, first;

		wr(CR, CR_NODMA | CR_PAGE1 | CR_START);
		curr = rd(CURR);
		wr(CR, CR_NODMA | CR_START);
		page = rd(BNRY) + 1;
		if (page >= rx_stop)
			page = RX_START;
		if (page == curr)
			break;

		card_read((u8 *)hdr, page * 256, 4);
		next = hdr[0] & 0xff;
		len = (hdr[1] >> 8 | hdr[1] << 8) & 0xffff;	/* little-endian */
		len -= 4;
		if (next < RX_START || next >= rx_stop || len < 14 || len > 1518) {
			/* a corrupt header: drop everything */
			ne_stats.rx_errors++;
			wr(BNRY, curr == RX_START ? rx_stop - 1 : curr - 1);
			break;
		}
		if (rxq_head - rxq_tail < RXQ) {
			u8 *d = rxq[rxq_head % RXQ].data;
			u32 off = page * 256 + 4, end = rx_stop * 256;

			first = end - off < (u32)len ? (int)(end - off) : len;
			card_read(d, off, first);
			if (first < len)
				card_read(d + first, RX_START * 256, len - first);
			rxq[rxq_head % RXQ].len = len;
			rxq_head++;
			ne_stats.rx_packets++;
		} else {
			ne_stats.rx_dropped++;
		}
		wr(BNRY, next == RX_START ? rx_stop - 1 : next - 1);
	}
}

/* Receiver overrun: the DP8390 datasheet's recovery sequence. */
static void overrun(void)
{
	int resend = rd(CR) & CR_TXP, n;

	ne_stats.overruns++;
	wr(CR, CR_NODMA | CR_STOP);
	for (n = 0; n < 1000 && !(rd(ISR) & ISR_RST); n++)
		delay(10);
	wr(RBCR0, 0);
	wr(RBCR1, 0);
	if (resend && (rd(ISR) & (ISR_PTX | ISR_TXE)))
		resend = 0;
	wr(TCR, 0x02);			/* loopback while we catch up */
	wr(CR, CR_NODMA | CR_START);
	drain_ring();
	wr(ISR, ISR_OVW);
	wr(TCR, 0x00);
	if (resend)
		wr(CR, CR_NODMA | CR_START | CR_TXP);
}

/* Interrupt, or a poll: handle whatever the chip has flagged. */
void ne_service(void)
{
	int guard = 8;

	if (!regs)
		return;
	while (guard--) {
		u8 isr = rd(ISR);

		if (!(isr & (ISR_PRX | ISR_RXE | ISR_OVW | ISR_PTX | ISR_TXE)))
			break;
		if (isr & ISR_OVW) {
			overrun();
			continue;
		}
		wr(ISR, isr & (ISR_PRX | ISR_RXE | ISR_PTX | ISR_TXE));
		if (isr & ISR_RXE)
			ne_stats.rx_errors++;
		if (isr & ISR_TXE)
			ne_stats.tx_errors++;
		if (isr & (ISR_PRX | ISR_RXE))
			drain_ring();
	}
}

/* VIA2 slot interrupt: ours if our slot's line is low. */
void ne_irq(void)
{
	ne_stats.irqs++;
	ne_service();
}

int ne_slot(void)
{
	return regs ? slot : 0;
}

int ne_recv(u8 *buf)
{
	int len;
	u16 sr = irq_save();

	if (rxq_tail == rxq_head) {
		irq_restore(sr);
		return 0;
	}
	len = rxq[rxq_tail % RXQ].len;
	memcpy(buf, rxq[rxq_tail % RXQ].data, len);
	rxq_tail++;
	irq_restore(sr);
	return len;
}

int ne_send(const u8 *frame, int len)
{
	u32 start = ticks;

	if (!regs)
		return 0;
	/* wait for the previous frame to go */
	while (rd(CR) & CR_TXP)
		if (ticks - start > 6) {
			ne_stats.tx_timeouts++;
			break;
		}
	if (len < 60)
		len = 60;		/* the pad is whatever was there */
	card_write(0, frame, len);
	wr(TPSR, 0);
	wr(TBCR0, len & 0xff);
	wr(TBCR1, len >> 8);
	wr(CR, CR_NODMA | CR_START | CR_TXP);
	ne_stats.tx_packets++;
	return 1;
}

/* Diagnostics for "net test": can the CPU write the transmit buffer in
 * card RAM, and at which access widths? Receive working while transmit
 * doesn't would fit writes that never land. Uses the transmit pages only. */
static int pattern_test(int width, void (*print)(void *, const char *, ...),
			void *arg)
{
	volatile u8 *b8 = (volatile u8 *)mem;
	volatile u16 *b16 = mem;
	volatile u32 *b32 = (volatile u32 *)mem;
	int i, bad = 0, n = TX_PAGES * 256;
	u32 first_at = 0, wrote = 0, read = 0;

	for (i = 0; i < n; i += width) {
		u32 v = (i * 0x9e3779b1u) ^ 0x5a5aa5a5u;

		if (width == 1)
			b8[i] = v;
		else if (width == 2)
			b16[i / 2] = v;
		else
			b32[i / 4] = v;
	}
	for (i = 0; i < n; i += width) {
		u32 v = (i * 0x9e3779b1u) ^ 0x5a5aa5a5u, r, m;

		if (width == 1) {
			r = b8[i];
			m = 0xff;
		} else if (width == 2) {
			r = b16[i / 2];
			m = 0xffff;
		} else {
			r = b32[i / 4];
			m = 0xffffffff;
		}
		if ((r & m) != (v & m)) {
			if (!bad++) {
				first_at = i;
				wrote = v & m;
				read = r & m;
			}
		}
	}
	if (bad)
		print(arg, "  %d-bit writes: %d of %d wrong; first at +%x wrote %x read %x\n",
		      width * 8, bad, n / width, first_at, wrote, read);
	else
		print(arg, "  %d-bit writes: all %d read back correctly\n", width * 8,
		      n / width);
	return bad;
}

static u8 card_mac[6];

void ne_test(int wide, u32 ip, u32 target,
	     void (*print)(void *, const char *, ...), void *arg)
{
	u8 f[60];
	int i, n;
	u8 isr = 0, tsr;
	u16 sr;

	if (!regs) {
		print(arg, "no card\n");
		return;
	}
	print(arg, "card RAM at %p, registers at %p, receive ring pages %x-%x\n",
	      (u32)mem, (u32)regs, RX_START, rx_stop);
	pattern_test(2, print, arg);
	pattern_test(1, print, arg);
	if (wide)
		pattern_test(4, print, arg);

	/* A broadcast ARP request. Interrupts stay off so the handler doesn't
	   take the chip's transmit status before we read it. */
	memset(f, 0, sizeof(f));
	memset(f, 0xff, 6);
	memcpy(f + 6, card_mac, 6);
	f[12] = 0x08; f[13] = 0x06;		/* ARP */
	f[15] = 1;				/* Ethernet */
	f[16] = 0x08;				/* IPv4 */
	f[18] = 6; f[19] = 4;
	f[21] = 1;				/* request */
	memcpy(f + 22, card_mac, 6);
	for (i = 0; i < 4; i++) {
		f[28 + i] = ip >> (24 - 8 * i);
		f[38 + i] = target >> (24 - 8 * i);
	}
	print(arg, "sending an ARP request for %u.%u.%u.%u\n", target >> 24,
	      target >> 16 & 255, target >> 8 & 255, target & 255);
	sr = irq_save();
	wr(ISR, ISR_PTX | ISR_TXE);
	ne_send(f, 60);
	for (n = 0; n < 200000; n++) {
		isr = rd(ISR);
		if (isr & (ISR_PTX | ISR_TXE))
			break;
	}
	tsr = rd(TSR);
	wr(ISR, isr & (ISR_PTX | ISR_TXE));
	irq_restore(sr);
	print(arg, "  ISR %x TSR %x after %d polls: %s\n", isr, tsr, n,
	      isr & ISR_PTX ? "the chip says it went out" :
	      isr & ISR_TXE ? "transmit error" : "the chip never finished");
	print(arg, "  TSR bits: 01 sent, 04 collided, 08 aborted, 10 carrier lost,\n"
	      "  20 FIFO underrun, 40 no heartbeat, 80 late collision\n");
	print(arg, "  read back from card: %02x %02x %02x %02x %02x %02x %02x %02x\n",
	      ((volatile u8 *)mem)[0], ((volatile u8 *)mem)[1], ((volatile u8 *)mem)[2],
	      ((volatile u8 *)mem)[3], ((volatile u8 *)mem)[12], ((volatile u8 *)mem)[13],
	      ((volatile u8 *)mem)[28], ((volatile u8 *)mem)[41]);
}

int ne_init(u8 *mac)
{
	struct nubus_func f;
	u32 base, off = 0xd0000, size = 0x8000;
	int i;

	if (!nubus_find(NUBUS_CAT_NETWORK, NUBUS_TYPE_ETHERNET, &f))
		return 0;
	if (!nubus_get(&f, NUBUS_RES_MAC_ADDRESS, mac, 6))
		return 0;
	nubus_get(&f, NUBUS_RES_MINOR_BASE, &off, 4);
	nubus_get(&f, NUBUS_RES_MINOR_LENGTH, &size, 4);
	if (size > 0x8000)
		size = 0x8000;		/* PSTOP is 8 bits of 256-byte pages */
	slot = f.slot;
	memcpy(card_mac, mac, 6);
	base = NUBUS_SLOT_ADDR(slot) | (u32)slot << 20;	/* 24-bit style */
	mem = (volatile u16 *)(base + off);
	regs = (volatile u8 *)(base + off + 0x10000);
	rx_stop = size / 256;

	wr(CR, CR_NODMA | CR_STOP);
	delay(2000);
	wr(DCR, 0x49);			/* word transfers, FIFO 8 bytes */
	wr(RBCR0, 0);
	wr(RBCR1, 0);
	wr(RCR, 0x20);			/* monitor only while we set up */
	wr(TCR, 0x02);			/* internal loopback */
	wr(TPSR, 0);
	wr(PSTART, RX_START);
	wr(BNRY, rx_stop - 1);
	wr(PSTOP, rx_stop);
	wr(ISR, 0xff);
	wr(IMR, 0);
	wr(CR, CR_NODMA | CR_PAGE1 | CR_STOP);
	for (i = 0; i < 6; i++)
		wr(PAR0 + i, mac[i]);
	for (i = 0; i < 8; i++)
		wr(MAR0 + i, 0);
	wr(CURR, RX_START);
	wr(CR, CR_NODMA | CR_STOP);

	wr(ISR, 0xff);
	wr(IMR, ISR_PRX | ISR_PTX | ISR_RXE | ISR_TXE | ISR_OVW);
	wr(CR, CR_NODMA | CR_START);
	wr(TCR, 0x00);
	wr(RCR, 0x04);			/* our address and broadcasts */
	return 1;
}

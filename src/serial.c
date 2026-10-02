/* serial.c - Z8530 SCC channel A (the modem port), 9600 8N1, polled.
 * Used for the kernel log; it shows up on MAME's bitbanger and on a real
 * serial cable. */
#include "kernel.h"

#define SCC_ACTRL	2	/* channel A control, from the SCC base */
#define SCC_ADATA	6	/* channel A data */
#define RR0_TX_EMPTY	0x04
#define RR0_RX_AVAIL	0x01

/* The SCC needs about 2.2 us between accesses on a real Mac. */
static void scc_delay(void)
{
	for (volatile int i = 0; i < 8; i++)
		;
}

static void scc_write(u8 reg, u8 val)
{
	volatile u8 *ctrl = mach.scc + SCC_ACTRL;

	*ctrl = reg;
	scc_delay();
	*ctrl = val;
	scc_delay();
}

static int ready;			/* initialised and transmitting */

void serial_init(void)
{
	if (!mach.scc)
		return;
	ready = 1;
	(void)mach.scc[SCC_ACTRL];	/* sync the register pointer */
	scc_delay();
	scc_write(9, 0x80);		/* reset channel A */
	scc_write(4, 0x44);		/* x16 clock, 1 stop bit, no parity */
	scc_write(3, 0xc0);		/* 8 bits, receiver off for now */
	scc_write(5, 0x60);		/* 8 bits, transmitter off for now */
	scc_write(9, 0x00);		/* no interrupts */
	scc_write(10, 0x00);		/* NRZ */
	scc_write(11, 0x50);		/* rx and tx clock = baud rate generator */
	scc_write(12, 10);		/* 3.6864 MHz / (2 * 16 * 9600) - 2 */
	scc_write(13, 0);
	scc_write(14, 0x01);		/* BRG on, clocked from RTxC */
	scc_write(3, 0xc1);		/* receiver on */
	scc_write(5, 0xea);		/* transmitter on, DTR and RTS */
	scc_write(15, 0x00);
	scc_write(1, 0x00);
}

/* Never wait long: with no clock on the port the transmitter would never
 * empty, and the log must not hang the machine. */
void serial_putc(char c)
{
	volatile u8 *ctrl = mach.scc + SCC_ACTRL;
	int n = 0;

	if (!ready)
		return;
	if (c == '\n')
		serial_putc('\r');
	while (!(*ctrl & RR0_TX_EMPTY)) {
		scc_delay();
		if (++n > 20000) {
			ready = 0;	/* a dead port: stop using it */
			return;
		}
	}
	scc_delay();
	mach.scc[SCC_ADATA] = c;
	scc_delay();
}

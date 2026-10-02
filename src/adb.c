/* adb.c - Apple Desktop Bus on the SE/30.
 *
 * The CPU talks to the ADB transceiver through VIA1's shift register. Two
 * port-B outputs (ST0/ST1) step the transceiver through a transaction and
 * a port-B input (/CTLR_IRQ) says it has data, or that the device didn't
 * answer. Each byte moved raises the VIA's shift-register interrupt:
 *
 *   our command:      CMD -> EVEN -> ODD -> EVEN ... -> IDLE
 *   autopoll packet:  IDLE -> EVEN -> ODD -> ... -> IDLE
 *
 * Once idle, the transceiver keeps repeating the last Talk command by
 * itself and interrupts only when the device answers, so a quiet bus costs
 * nothing. We switch it between the keyboard and the mouse when the other
 * one raises a service request.
 */
#include "kernel.h"
#include "input.h"

#define CTLR_IRQ	0x08	/* port B: transceiver has data (active low) */
#define ST_MASK		0x30	/* port B: transaction state lines */
#define ST_CMD		0x00
#define ST_EVEN		0x10
#define ST_ODD		0x20
#define ST_IDLE		0x30

#define ACR_SR_MASK	0x1c
#define ACR_SR_IN	0x0c	/* shift in on the external clock */
#define ACR_SR_OUT	0x10	/* with SR_IN: shift out on the external clock */

#define ADB_TALK(a, r)	((u8)(((a) << 4) | 0x0c | (r)))
#define ADB_ADDR(c)	((c) >> 4)
#define ADB_IS_TALK(c)	(((c) & 0x0c) == 0x0c)

#define ADDR_KEYBOARD	2
#define ADDR_MOUSE	3

enum { IDLE, SENDING, READING };

static volatile u8 *via;
static int state = IDLE;
static u8 out[8];		/* command to send, and Listen data */
static int out_len, out_pos;	/* out_len 0: nothing to send */
static u8 in[16];		/* command byte, then what came back */
static int in_len;
static int bus_timeout, srq;
static u8 last_cmd;		/* last command the transceiver accepted */
static u8 last_talk;		/* ... the last Talk among them */
static u8 last_poll = ADB_TALK(ADDR_MOUSE, 0);	/* ... the last Talk R0 */

u32 adb_packets, adb_irqs;	/* statistics */

static void set_state(u8 st)
{
	via[VIA_B] = (via[VIA_B] & ~ST_MASK) | st;
}

static void start(void)
{
	via[VIA_ACR] |= ACR_SR_OUT;
	via[VIA_SR] = out[0];
	set_state(ST_CMD);
	state = SENDING;
	out_pos = 1;
	bus_timeout = srq = 0;
}

/* Point the transceiver's autopolling at the device that needs it. */
static void queue_poll(void)
{
	u8 addr = ADB_ADDR(last_poll);
	u8 cmd;

	if (srq && last_cmd == last_poll)
		addr = addr == ADDR_MOUSE ? ADDR_KEYBOARD : ADDR_MOUSE;
	cmd = ADB_TALK(addr, 0);
	if (cmd == last_cmd)
		return;			/* it is already repeating this one */
	out[0] = cmd;
	out_len = 1;
}

static void packet(void)
{
	adb_packets++;
	if (in_len < 3 || in[0] != last_poll)
		return;
	switch (ADB_ADDR(in[0])) {
	case ADDR_MOUSE:
		/* bit 7: button up; bits 6-0: signed 7-bit Y, then X */
		mouse_event((s8)(in[2] << 1) >> 1, (s8)(in[1] << 1) >> 1,
			    !(in[1] & 0x80));
		break;
	case ADDR_KEYBOARD:
		/* two key transitions; 0xff is "none" */
		key_event(in[1]);
		if (in[2] != 0xff)
			key_event(in[2]);
		break;
	}
}

/* Shift-register interrupt: one byte has moved. */
void adb_irq(void)
{
	u8 status = via[VIA_B] & (ST_MASK | CTLR_IRQ);
	u8 x;

	adb_irqs++;

	switch (state) {
	case IDLE:
		/* An autopoll reply starts with the polled command byte. The
		   transceiver flags it with /CTLR_IRQ only if its unused RB0
		   and RB1 pins read high (they read low in MAME); otherwise
		   it is the transceiver's periodic re-send, so ignore it. */
		bus_timeout = srq = 0;
		x = via[VIA_SR];
		if ((status & CTLR_IRQ) && x != last_talk)
			break;
		in[0] = x;
		in_len = 1;
		state = READING;
		set_state(ST_EVEN);
		break;

	case SENDING:
		if (status == (ST_CMD | CTLR_IRQ)) {
			last_cmd = out[0];	/* command byte accepted */
			if (ADB_IS_TALK(last_cmd)) {
				last_talk = last_cmd;
				if ((last_cmd & 0x0f) == 0x0c)
					last_poll = last_cmd;
			}
		}
		if (status == ST_CMD) {
			/* The transceiver had an autopoll reply waiting and
			   ate our command; read the reply (its first byte was
			   lost while the register was shifting out) and send
			   the command again afterwards. */
			via[VIA_ACR] &= ~ACR_SR_OUT;
			(void)via[VIA_SR];
			in[0] = last_talk;
			in_len = 1;
			state = READING;
		} else if (out_pos < out_len) {
			via[VIA_SR] = out[out_pos++];
		} else {
			out_len = 0;
			if (!ADB_IS_TALK(out[0])) {
				state = IDLE;
				break;
			}
			via[VIA_ACR] &= ~ACR_SR_OUT;
			(void)via[VIA_SR];
			in[0] = out[0];
			in_len = 1;
			state = READING;
		}
		if ((via[VIA_B] & ST_MASK) == ST_CMD)
			set_state(ST_EVEN);
		else
			via[VIA_B] ^= ST_MASK;
		break;

	case READING:
		x = via[VIA_SR];
		if (!(status & CTLR_IRQ)) {
			if (status == ST_EVEN && in_len == 1) {
				bus_timeout = 1;	/* nobody answered */
			} else if (status == ST_ODD && in_len == 2) {
				srq = 1;	/* another device wants a turn */
			} else {
				state = IDLE;
				if (!bus_timeout)
					packet();
				break;
			}
		}
		if (in_len < (int)sizeof(in))
			in[in_len++] = x;
		via[VIA_B] ^= ST_MASK;		/* EVEN <-> ODD */
		break;
	}

	if (state == IDLE) {
		if (!out_len)
			queue_poll();
		if (out_len) {
			start();
		} else {
			via[VIA_ACR] &= ~ACR_SR_OUT;
			(void)via[VIA_SR];
			set_state(ST_IDLE);
		}
	}
}

void adb_init(void)
{
	via = mach.via1;
	via[VIA_DDRB] = (via[VIA_DDRB] | ST_MASK) & ~CTLR_IRQ;
	via[VIA_ACR] = (via[VIA_ACR] & ~ACR_SR_MASK) | ACR_SR_IN;
	set_state(ST_IDLE);
	(void)via[VIA_SR];
	via[VIA_IFR] = VIA_INT_SR;
	via[VIA_IER] = 0x80 | VIA_INT_SR;

	/* Mac OS may have left the transceiver polling anything; start it
	   on the mouse. Interrupts are still masked here. */
	last_cmd = 0;
	out[0] = ADB_TALK(ADDR_MOUSE, 0);
	out_len = 1;
	start();
}

void adb_debug(void)
{

	kprintf("adb: %u irqs, %u packets, state %d, port B %x, polling %x\n",
		adb_irqs, adb_packets, state, via[VIA_B], last_poll);
}

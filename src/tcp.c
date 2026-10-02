/* tcp.c - TCP, the client side: enough for telnet and friends.
 *
 * Received data goes straight to the application's callback, so only
 * in-order segments are accepted and the window we advertise never
 * shrinks. Sent data waits in a buffer until it is acknowledged; a timer
 * resends everything unacknowledged (go-back-N) with exponential backoff.
 */
#include "kernel.h"
#include "net.h"

#define MAXCONN		4
#define SBUF		16384		/* unacknowledged plus unsent data */
#define RWIN		16384		/* the window we advertise */
#define RTO_START	60		/* ticks */
#define RTO_MAX		(60 * 30)
#define MAX_RETRIES	10

#define FIN	0x01
#define SYN	0x02
#define RST	0x04
#define PSH	0x08
#define ACK	0x10

enum { CLOSED, SYN_SENT, ESTABLISHED, FIN_WAIT_1, FIN_WAIT_2, CLOSING,
       TIME_WAIT, CLOSE_WAIT, LAST_ACK };

static const char *const state_names[] = {
	"closed", "syn-sent", "established", "fin-wait-1", "fin-wait-2",
	"closing", "time-wait", "close-wait", "last-ack",
};

struct tcp {
	int state;
	u32 rip;
	u16 rport, lport;
	u32 iss, snd_una, snd_nxt, snd_wnd, rcv_nxt;
	u16 mss;
	u8 *sbuf;			/* data from snd_una on */
	int slen;
	int fin_wanted, fin_sent;
	int ack_now;
	u32 rto, timer;			/* resend deadline, 0: none */
	int retries;
	u32 tw_until;
	int user_closed;
	void (*recv)(struct tcp *, const u8 *, int);
	void (*event)(struct tcp *, int);
	void *arg;
};

static struct tcp conns[MAXCONN];
static u16 next_port;

static int seq_lt(u32 a, u32 b) { return (s32)(a - b) < 0; }
static int seq_le(u32 a, u32 b) { return (s32)(a - b) <= 0; }

static void send_seg(u32 rip, u16 lport, u16 rport, u32 seq, u32 ack,
		     u8 flags, const u8 *data, int len, int mss_opt)
{
	u8 ph[12], *p = ip_payload();
	int hl = mss_opt ? 24 : 20;
	u32 sum;

	put16(p, lport);
	put16(p + 2, rport);
	put32(p + 4, seq);
	put32(p + 8, ack);
	p[12] = (hl / 4) << 4;
	p[13] = flags;
	put16(p + 14, RWIN);
	put16(p + 16, 0);
	put16(p + 18, 0);
	if (mss_opt) {
		p[20] = 2;
		p[21] = 4;
		put16(p + 22, 1460);
	}
	if (len)
		memcpy(p + hl, data, len);
	put32(ph, nif.ip);
	put32(ph + 4, rip);
	ph[8] = 0;
	ph[9] = IP_TCP;
	put16(ph + 10, hl + len);
	sum = checksum_add(checksum_add(0, ph, 12), p, hl + len);
	put16(p + 16, checksum_fold(sum));
	ip_send(rip, IP_TCP, hl + len);
}

static void send_flags(struct tcp *c, u32 seq, u8 flags, const u8 *data,
		       int len)
{
	send_seg(c->rip, c->lport, c->rport, seq, c->rcv_nxt, flags, data, len,
		 flags & SYN);
	if (flags & ACK)
		c->ack_now = 0;
}

static void arm_timer(struct tcp *c)
{
	if (!c->timer)
		c->timer = ticks + c->rto;
}

/* Send whatever the window allows, then our FIN if one is due. */
static void output(struct tcp *c)
{
	for (;;) {
		u32 off = c->snd_nxt - c->snd_una;
		int n = c->slen - (int)off;
		s32 room = (s32)(c->snd_una + c->snd_wnd - c->snd_nxt);

		if (c->fin_sent || n <= 0 || room <= 0)
			break;
		if (n > room)
			n = room;
		if (n > c->mss)
			n = c->mss;
		send_flags(c, c->snd_nxt, ACK | PSH, c->sbuf + off, n);
		c->snd_nxt += n;
		arm_timer(c);
	}
	if (c->fin_wanted && !c->fin_sent &&
	    c->snd_nxt - c->snd_una == (u32)c->slen) {
		send_flags(c, c->snd_nxt, FIN | ACK, NULL, 0);
		c->snd_nxt++;
		c->fin_sent = 1;
		arm_timer(c);
	}
}

static void notify(struct tcp *c, int ev)
{
	if (c->event && !c->user_closed)
		c->event(c, ev);
}

static void drop(struct tcp *c)
{
	c->state = CLOSED;
	if (c->sbuf)
		kfree(c->sbuf);
	c->sbuf = NULL;
}

struct tcp *tcp_connect(u32 ip, u16 port,
			void (*recv)(struct tcp *, const u8 *, int),
			void (*event)(struct tcp *, int), void *arg)
{
	struct tcp *c = NULL;
	int i;

	for (i = 0; i < MAXCONN; i++)
		if (conns[i].state == CLOSED) {
			c = &conns[i];
			break;
		}
	if (!c || !nif.up)
		return NULL;
	memset(c, 0, sizeof(*c));
	c->sbuf = kmalloc(SBUF);
	if (!c->sbuf)
		return NULL;
	if (!next_port)
		next_port = 49152 + (ticks & 0x3fff);
	c->lport = next_port++;
	if (next_port < 49152)
		next_port = 49152;
	c->rip = ip;
	c->rport = port;
	c->iss = ticks * 250000 + c->lport;
	c->snd_una = c->iss;
	c->snd_nxt = c->iss + 1;
	c->snd_wnd = 1;
	c->mss = 536;
	c->rto = RTO_START;
	c->recv = recv;
	c->event = event;
	c->arg = arg;
	c->state = SYN_SENT;
	send_flags(c, c->iss, SYN, NULL, 0);
	arm_timer(c);
	return c;
}

int tcp_write(struct tcp *c, const void *data, int len)
{
	if (c->state != ESTABLISHED && c->state != CLOSE_WAIT)
		return -1;
	if (len > SBUF - c->slen)
		len = SBUF - c->slen;
	memcpy(c->sbuf + c->slen, data, len);
	c->slen += len;
	output(c);
	return len;
}

void tcp_close(struct tcp *c)
{
	c->user_closed = 1;
	switch (c->state) {
	case SYN_SENT:
		drop(c);
		return;
	case ESTABLISHED:
		c->state = FIN_WAIT_1;
		break;
	case CLOSE_WAIT:
		c->state = LAST_ACK;
		break;
	default:
		return;
	}
	c->fin_wanted = 1;
	output(c);
}

void tcp_abort(struct tcp *c)
{
	if (c->state != CLOSED && c->state != SYN_SENT)
		send_flags(c, c->snd_nxt, RST | ACK, NULL, 0);
	c->user_closed = 1;
	drop(c);
}

void *tcp_arg(struct tcp *c)
{
	return c->arg;
}

int tcp_state(struct tcp *c)
{
	return c->state;
}

/* Answer a segment for no connection with a reset. */
static void reset_reply(u32 src, const u8 *p, int dlen)
{
	u8 flags = p[13];
	u32 seq = get32(p + 4), ack = get32(p + 8);

	if (flags & RST)
		return;
	if (flags & ACK)
		send_seg(src, get16(p + 2), get16(p), ack, 0, RST, NULL, 0, 0);
	else
		send_seg(src, get16(p + 2), get16(p), 0,
			 seq + dlen + !!(flags & SYN) + !!(flags & FIN),
			 RST | ACK, NULL, 0, 0);
}

static void parse_mss(struct tcp *c, const u8 *p, int hl)
{
	int i = 20;

	while (i < hl) {
		u8 kind = p[i];

		if (kind == 0)
			break;
		if (kind == 1) {
			i++;
			continue;
		}
		if (i + 1 >= hl || p[i + 1] < 2)
			break;
		if (kind == 2 && p[i + 1] == 4 && i + 3 < hl) {
			u16 m = get16(p + i + 2);

			if (m >= 64 && m < 1460)
				c->mss = m;
			else if (m >= 1460)
				c->mss = 1460;
		}
		i += p[i + 1];
	}
}

void tcp_input(u32 src, u32 dst, const u8 *p, int len)
{
	u8 ph[12], flags;
	struct tcp *c = NULL;
	u32 seq, ack;
	int hl, dlen, i;
	const u8 *data;

	if (len < 20)
		return;
	put32(ph, src);
	put32(ph + 4, dst);
	ph[8] = 0;
	ph[9] = IP_TCP;
	put16(ph + 10, len);
	if (checksum_fold(checksum_add(checksum_add(0, ph, 12), p, len)))
		return;
	hl = (p[12] >> 4) * 4;
	if (hl < 20 || hl > len)
		return;
	flags = p[13];
	seq = get32(p + 4);
	ack = get32(p + 8);
	data = p + hl;
	dlen = len - hl;

	for (i = 0; i < MAXCONN; i++)
		if (conns[i].state != CLOSED && conns[i].rip == src &&
		    conns[i].rport == get16(p) && conns[i].lport == get16(p + 2)) {
			c = &conns[i];
			break;
		}
	if (!c) {
		reset_reply(src, p, dlen);
		return;
	}

	if (c->state == SYN_SENT) {
		if ((flags & ACK) && ack != c->iss + 1) {
			if (!(flags & RST))
				send_seg(src, c->lport, c->rport, ack, 0, RST, NULL, 0, 0);
			return;
		}
		if (flags & RST) {
			if (flags & ACK) {
				notify(c, TCP_REFUSED);
				drop(c);
			}
			return;
		}
		if ((flags & SYN) && (flags & ACK)) {
			c->rcv_nxt = seq + 1;
			c->snd_una = ack;
			c->snd_wnd = get16(p + 14);
			parse_mss(c, p, hl);
			c->state = ESTABLISHED;
			c->timer = 0;
			c->retries = 0;
			c->rto = RTO_START;
			c->ack_now = 1;
			notify(c, TCP_CONNECTED);
			output(c);
		}
		return;
	}

	if (flags & RST) {
		/* believe it if it's in the window */
		if (seq_le(c->rcv_nxt, seq) && seq_lt(seq, c->rcv_nxt + RWIN)) {
			notify(c, TCP_RESET);
			drop(c);
		}
		return;
	}
	if (flags & SYN) {
		c->ack_now = 1;			/* a repeated SYN-ACK */
		return;
	}

	/* acknowledgements */
	if ((flags & ACK) && seq_lt(c->snd_una, ack) && seq_le(ack, c->snd_nxt)) {
		u32 n = ack - c->snd_una;
		int data_acked = n > (u32)c->slen ? c->slen : (int)n;

		memmove(c->sbuf, c->sbuf + data_acked, c->slen - data_acked);
		c->slen -= data_acked;
		c->snd_una = ack;
		c->retries = 0;
		c->rto = RTO_START;
		c->timer = 0;
		if (c->snd_una != c->snd_nxt)
			arm_timer(c);
		if (c->fin_sent && ack == c->snd_nxt) {
			/* our FIN is acknowledged */
			if (c->state == FIN_WAIT_1)
				c->state = FIN_WAIT_2;
			else if (c->state == CLOSING) {
				c->state = TIME_WAIT;
				c->tw_until = ticks + 120;
			} else if (c->state == LAST_ACK) {
				drop(c);
				return;
			}
		}
	}
	if (flags & ACK)
		c->snd_wnd = get16(p + 14);

	/* data, in order only; trim what we already have */
	if (dlen && seq_lt(seq, c->rcv_nxt)) {
		u32 skip = c->rcv_nxt - seq;

		if (skip >= (u32)dlen) {
			dlen = 0;
		} else {
			data += skip;
			dlen -= skip;
			seq = c->rcv_nxt;
		}
		c->ack_now = 1;
	}
	if (dlen) {
		if (seq == c->rcv_nxt && (c->state == ESTABLISHED ||
					  c->state == FIN_WAIT_1 ||
					  c->state == FIN_WAIT_2)) {
			c->rcv_nxt += dlen;
			if (c->recv && !c->user_closed)
				c->recv(c, data, dlen);
		}
		c->ack_now = 1;
	}
	if ((flags & FIN) && seq + dlen == c->rcv_nxt) {
		c->rcv_nxt++;
		c->ack_now = 1;
		switch (c->state) {
		case ESTABLISHED:
			c->state = CLOSE_WAIT;
			notify(c, TCP_CLOSED);
			break;
		case FIN_WAIT_1:
			c->state = CLOSING;
			break;
		case FIN_WAIT_2:
			c->state = TIME_WAIT;
			c->tw_until = ticks + 120;
			break;
		}
	}
	if (c->state != CLOSED)
		output(c);
}

void tcp_flush_acks(void)
{
	int i;

	for (i = 0; i < MAXCONN; i++) {
		struct tcp *c = &conns[i];

		if (c->state != CLOSED && c->state != SYN_SENT && c->ack_now)
			send_flags(c, c->snd_nxt, ACK, NULL, 0);
	}
}

void tcp_timer(void)
{
	int i;

	for (i = 0; i < MAXCONN; i++) {
		struct tcp *c = &conns[i];

		if (c->state == CLOSED)
			continue;
		if (c->state == TIME_WAIT) {
			if ((s32)(ticks - c->tw_until) >= 0)
				drop(c);
			continue;
		}
		if (!c->timer || (s32)(ticks - c->timer) < 0)
			continue;
		if (++c->retries > MAX_RETRIES) {
			notify(c, TCP_TIMEOUT);
			drop(c);
			continue;
		}
		c->rto = c->rto * 2 > RTO_MAX ? RTO_MAX : c->rto * 2;
		c->timer = 0;
		if (c->state == SYN_SENT) {
			send_flags(c, c->iss, SYN, NULL, 0);
		} else {
			/* go back and send it all again */
			c->snd_nxt = c->snd_una;
			c->fin_sent = 0;
			if (c->snd_wnd == 0)
				c->snd_wnd = 1;		/* probe a closed window */
			output(c);
		}
		arm_timer(c);
	}
}

void tcp_list(void (*print)(void *, const char *, ...), void *arg)
{
	char ip[16];
	int i;

	for (i = 0; i < MAXCONN; i++) {
		struct tcp *c = &conns[i];

		if (c->state == CLOSED)
			continue;
		print(arg, "  %u -> %s:%u  %s  unacked %u\n", c->lport,
		      ip_str(c->rip, ip), c->rport, state_names[c->state],
		      c->snd_nxt - c->snd_una);
	}
}

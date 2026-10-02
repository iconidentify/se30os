/* net.c - Ethernet, ARP, IPv4, ICMP and UDP.
 *
 * Packets come off the card in the interrupt handler and are processed
 * here, in net_poll(), from the main loop. Sending builds one frame at a
 * time in a static buffer: callers fill ip_payload() and call ip_send().
 */
#include "kernel.h"
#include "net.h"

struct netif nif;

#define ETH_HLEN	14
#define ETH_IP		0x0800
#define ETH_ARP		0x0806

static u8 frame[1536] __attribute__((aligned(4)));	/* outgoing */
static u8 rxbuf[1536] __attribute__((aligned(4)));	/* incoming */
static u16 ip_id;
static const u8 bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* ---- checksums ---- */

u32 checksum_add(u32 sum, const u8 *p, int len)
{
	if (!((u32)p & 1)) {
		const u16 *w = (const u16 *)p;

		for (; len > 1; len -= 2)
			sum += *w++;
		p = (const u8 *)w;
	} else {
		for (; len > 1; len -= 2, p += 2)
			sum += p[0] << 8 | p[1];
	}
	if (len)
		sum += p[0] << 8;
	return sum;
}

u16 checksum_fold(u32 sum)
{
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return ~sum & 0xffff;
}

/* ---- addresses ---- */

char *ip_str(u32 ip, char *buf)
{
	ksnprintf(buf, 16, "%u.%u.%u.%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255,
		  ip & 255);
	return buf;
}

int ip_parse(const char *s, u32 *ip)
{
	u32 v = 0;
	int i;

	for (i = 0; i < 4; i++) {
		int n = 0, digits = 0;

		while (*s >= '0' && *s <= '9') {
			n = n * 10 + *s++ - '0';
			digits++;
		}
		if (!digits || n > 255)
			return 0;
		v = v << 8 | n;
		if (i < 3 && *s++ != '.')
			return 0;
	}
	if (*s)
		return 0;
	*ip = v;
	return 1;
}

/* ---- ARP ---- */

#define NARP 16

static struct {
	u32 ip;
	u8 mac[6];
	u32 used;
} arp_cache[NARP];

/* One frame may wait for its next hop's address. */
static struct {
	u32 hop;
	int len;
	u32 sent;
	int tries;
	u8 frame[1536];
} waiting;

static u8 *arp_lookup(u32 ip)
{
	int i;

	for (i = 0; i < NARP; i++)
		if (arp_cache[i].ip == ip && ip) {
			arp_cache[i].used = ticks;
			return arp_cache[i].mac;
		}
	return NULL;
}

static void arp_learn(u32 ip, const u8 *mac)
{
	int i, victim = 0;

	for (i = 0; i < NARP; i++) {
		if (arp_cache[i].ip == ip)
			break;
		if (arp_cache[i].used < arp_cache[victim].used)
			victim = i;
	}
	if (i == NARP)
		i = victim;
	arp_cache[i].ip = ip;
	memcpy(arp_cache[i].mac, mac, 6);
	arp_cache[i].used = ticks;
}

static void arp_send(int op, const u8 *dmac, u32 dip)
{
	u8 f[60] __attribute__((aligned(4)));

	memset(f, 0, sizeof(f));
	memcpy(f, op == 1 ? bcast : dmac, 6);
	memcpy(f + 6, nif.mac, 6);
	put16(f + 12, ETH_ARP);
	put16(f + 14, 1);		/* Ethernet */
	put16(f + 16, ETH_IP);
	f[18] = 6;
	f[19] = 4;
	put16(f + 20, op);
	memcpy(f + 22, nif.mac, 6);
	put32(f + 28, nif.ip);
	if (op == 2)
		memcpy(f + 32, dmac, 6);
	put32(f + 38, dip);
	ne_send(f, 60);
}

static void arp_input(const u8 *p, int len)
{
	u32 spa, tpa;
	int op;

	if (len < 28 || get16(p) != 1 || get16(p + 2) != ETH_IP)
		return;
	op = get16(p + 6);
	spa = get32(p + 14);
	tpa = get32(p + 24);
	if (tpa != nif.ip)
		return;
	arp_learn(spa, p + 8);
	if (op == 1)
		arp_send(2, p + 8, spa);
	if (waiting.len && waiting.hop == spa) {
		memcpy(waiting.frame, p + 8, 6);
		ne_send(waiting.frame, waiting.len);
		waiting.len = 0;
	}
}

static void arp_retry(void)
{
	if (!waiting.len || ticks - waiting.sent < 30)
		return;
	if (++waiting.tries > 4) {
		waiting.len = 0;	/* give up; TCP will try again */
		return;
	}
	waiting.sent = ticks;
	arp_send(1, NULL, waiting.hop);
}

/* ---- IP ---- */

u8 *ip_payload(void)
{
	return frame + ETH_HLEN + 20;
}

int ip_send(u32 dst, int proto, int len)
{
	u8 *h = frame + ETH_HLEN, *mac;
	u32 hop;
	int flen = ETH_HLEN + 20 + len;

	if (!nif.up)
		return 0;
	h[0] = 0x45;
	h[1] = 0;
	put16(h + 2, 20 + len);
	put16(h + 4, ip_id++);
	put16(h + 6, 0x4000);		/* don't fragment */
	h[8] = 64;
	h[9] = proto;
	put16(h + 10, 0);
	put32(h + 12, nif.ip);
	put32(h + 16, dst);
	put16(h + 10, checksum_fold(checksum_add(0, h, 20)));

	memcpy(frame + 6, nif.mac, 6);
	put16(frame + 12, ETH_IP);
	if (flen < 60) {
		memset(frame + flen, 0, 60 - flen);
		flen = 60;
	}
	if (dst == 0xffffffff) {
		memcpy(frame, bcast, 6);
		return ne_send(frame, flen);
	}
	hop = ((dst ^ nif.ip) & nif.mask) == 0 ? dst : nif.gw;
	mac = arp_lookup(hop);
	if (mac) {
		memcpy(frame, mac, 6);
		return ne_send(frame, flen);
	}
	/* park it until the ARP reply */
	memcpy(waiting.frame, frame, flen);
	waiting.len = flen;
	waiting.hop = hop;
	waiting.sent = ticks;
	waiting.tries = 0;
	arp_send(1, NULL, hop);
	return 1;
}

/* ---- ICMP ---- */

volatile u16 ping_reply_seq;
volatile u32 ping_reply_ticks;
static u16 ping_id;

static void icmp_input(u32 src, const u8 *p, int len)
{
	if (len < 8 || checksum_fold(checksum_add(0, p, len)))
		return;
	if (p[0] == 8) {			/* echo request */
		u8 *r = ip_payload();

		if (len > 1400)
			return;
		memcpy(r, p, len);
		r[0] = 0;
		put16(r + 2, 0);
		put16(r + 2, checksum_fold(checksum_add(0, r, len)));
		ip_send(src, IP_ICMP, len);
	} else if (p[0] == 0 && get16(p + 4) == ping_id) {
		ping_reply_seq = get16(p + 6);
		ping_reply_ticks = ticks;
	}
}

void icmp_ping(u32 dst, u16 id, u16 seq)
{
	u8 *r = ip_payload();
	int i, len = 64;

	ping_id = id;
	r[0] = 8;
	r[1] = 0;
	put16(r + 2, 0);
	put16(r + 4, id);
	put16(r + 6, seq);
	for (i = 8; i < len; i++)
		r[i] = i;
	put16(r + 2, checksum_fold(checksum_add(0, r, len)));
	ip_send(dst, IP_ICMP, len);
}

/* ---- UDP ---- */

#define NUDP 8

static struct {
	u16 port;
	udp_handler fn;
} udp_ports[NUDP];

int udp_bind(u16 port, udp_handler fn)
{
	int i;

	for (i = 0; i < NUDP; i++)
		if (!udp_ports[i].port) {
			udp_ports[i].port = port;
			udp_ports[i].fn = fn;
			return 1;
		}
	return 0;
}

void udp_unbind(u16 port)
{
	int i;

	for (i = 0; i < NUDP; i++)
		if (udp_ports[i].port == port)
			udp_ports[i].port = 0;
}

static u32 pseudo_sum(u32 src, u32 dst, int proto, int len)
{
	u8 ph[12];

	put32(ph, src);
	put32(ph + 4, dst);
	ph[8] = 0;
	ph[9] = proto;
	put16(ph + 10, len);
	return checksum_add(0, ph, 12);
}

int udp_send(u32 dst, u16 sport, u16 dport, const u8 *data, int len)
{
	u8 *u = ip_payload();
	u16 sum;

	put16(u, sport);
	put16(u + 2, dport);
	put16(u + 4, 8 + len);
	put16(u + 6, 0);
	memcpy(u + 8, data, len);
	sum = checksum_fold(checksum_add(pseudo_sum(nif.ip, dst, IP_UDP, 8 + len),
					 u, 8 + len));
	put16(u + 6, sum ? sum : 0xffff);
	return ip_send(dst, IP_UDP, 8 + len);
}

static void udp_input(u32 src, const u8 *p, int len)
{
	u16 dport;
	int i;

	if (len < 8 || get16(p + 4) > len)
		return;
	dport = get16(p + 2);
	for (i = 0; i < NUDP; i++)
		if (udp_ports[i].port == dport) {
			udp_ports[i].fn(src, get16(p), p + 8, get16(p + 4) - 8);
			return;
		}
}

/* ---- input ---- */

static void ip_input(const u8 *h, int len)
{
	int hl, tot;
	u32 src, dst;

	if (len < 20 || (h[0] >> 4) != 4)
		return;
	hl = (h[0] & 15) * 4;
	tot = get16(h + 2);
	if (hl < 20 || tot < hl || tot > len)
		return;
	if (get16(h + 6) & 0x3fff)
		return;				/* fragments: not wanted */
	if (checksum_fold(checksum_add(0, h, hl)))
		return;
	src = get32(h + 12);
	dst = get32(h + 16);
	if (dst != nif.ip && dst != 0xffffffff && dst != (nif.ip | ~nif.mask))
		return;
	switch (h[9]) {
	case IP_ICMP:
		icmp_input(src, h + hl, tot - hl);
		break;
	case IP_UDP:
		udp_input(src, h + hl, tot - hl);
		break;
	case IP_TCP:
		if (dst == nif.ip)
			tcp_input(src, dst, h + hl, tot - hl);
		break;
	}
}

static void eth_input(const u8 *f, int len)
{
	if (len < ETH_HLEN)
		return;
	switch (get16(f + 12)) {
	case ETH_ARP:
		arp_input(f + ETH_HLEN, len - ETH_HLEN);
		break;
	case ETH_IP:
		ip_input(f + ETH_HLEN, len - ETH_HLEN);
		break;
	}
}

void net_poll(void)
{
	static u32 last_tick;
	int len;
	u16 sr;

	if (!nif.up)
		return;
	/* in case the card's interrupt doesn't reach us */
	sr = irq_save();
	ne_service();
	irq_restore(sr);
	while ((len = ne_recv(rxbuf)) > 0)
		eth_input(rxbuf, len);
	tcp_flush_acks();
	if (ticks != last_tick) {
		last_tick = ticks;
		arp_retry();
		tcp_timer();
		tcp_flush_acks();
	}
}

void net_init(void)
{
	nif.ip = IP(10, 1, 1, 214);
	nif.mask = IP(255, 255, 255, 0);
	nif.gw = IP(10, 1, 1, 1);
	nif.dns = IP(10, 1, 1, 1);
	nif.up = ne_init(nif.mac);
	ip_id = ticks;
}

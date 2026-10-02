#ifndef NET_H
#define NET_H

/* ne.c: the DP8390 Ethernet card */
struct ne_stats {
	u32 irqs, rx_packets, tx_packets, rx_errors, tx_errors, rx_dropped,
	    overruns, tx_timeouts;
};
extern struct ne_stats ne_stats;
int ne_init(u8 *mac);
int ne_slot(void);
int ne_send(const u8 *frame, int len);
int ne_recv(u8 *buf);			/* 0 if nothing waiting */
void ne_service(void);
void ne_irq(void);
void ne_test(int wide, u32 ip, u32 target,
	     void (*print)(void *, const char *, ...), void *arg);

/* net.c: Ethernet, ARP, IPv4, ICMP, UDP */
struct netif {
	int up;
	u8 mac[6];
	u32 ip, mask, gw, dns;
};
extern struct netif nif;

#define IP(a, b, c, d)	((u32)(a) << 24 | (u32)(b) << 16 | (u32)(c) << 8 | (d))
#define IP_TCP	6
#define IP_UDP	17
#define IP_ICMP	1

static inline u16 get16(const u8 *p) { return p[0] << 8 | p[1]; }
static inline u32 get32(const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static inline void put16(u8 *p, u16 v) { p[0] = v >> 8; p[1] = v; }
static inline void put32(u8 *p, u32 v)
{
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

void net_init(void);
void net_poll(void);			/* main loop: packets and timers */
u32 checksum_add(u32 sum, const u8 *p, int len);
u16 checksum_fold(u32 sum);

/* Build an IP packet in the buffer ip_buf() returns (payload starts 20
   bytes in), then send it. */
u8 *ip_payload(void);
int ip_send(u32 dst, int proto, int len);

typedef void (*udp_handler)(u32 src, u16 sport, const u8 *data, int len);
int udp_bind(u16 port, udp_handler fn);
void udp_unbind(u16 port);
int udp_send(u32 dst, u16 sport, u16 dport, const u8 *data, int len);

/* ping: returns round-trip ticks, or -1 */
void icmp_ping(u32 dst, u16 id, u16 seq);
extern volatile u16 ping_reply_seq;
extern volatile u32 ping_reply_ticks;

char *ip_str(u32 ip, char *buf);
int ip_parse(const char *s, u32 *ip);

/* dns.c */
int dns_resolve(const char *name, u32 *ip);	/* blocks, polling the net */

/* tcp.c */
struct tcp;
enum { TCP_CONNECTED = 1, TCP_CLOSED, TCP_RESET, TCP_TIMEOUT, TCP_REFUSED };
struct tcp *tcp_connect(u32 ip, u16 port,
			void (*recv)(struct tcp *, const u8 *, int),
			void (*event)(struct tcp *, int), void *arg);
int tcp_write(struct tcp *c, const void *data, int len);
void tcp_close(struct tcp *c);
void tcp_abort(struct tcp *c);
void *tcp_arg(struct tcp *c);
int tcp_state(struct tcp *c);
void tcp_input(u32 src, u32 dst, const u8 *seg, int len);
void tcp_timer(void);
void tcp_flush_acks(void);
void tcp_list(void (*print)(void *, const char *, ...), void *arg);

#endif

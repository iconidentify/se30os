/* dns.c - look up a host's IPv4 address. */
#include "kernel.h"
#include "net.h"

#define DNS_PORT_LOCAL	53053

static volatile int done;
static u32 answer;
static u16 query_id;

/* Skip a (possibly compressed) name; returns the offset after it. */
static int skip_name(const u8 *d, int len, int i)
{
	while (i < len) {
		u8 n = d[i];

		if (n == 0)
			return i + 1;
		if ((n & 0xc0) == 0xc0)
			return i + 2;
		i += n + 1;
	}
	return len;
}

static void reply(u32 src, u16 sport, const u8 *d, int len)
{
	int qd, an, i;

	if (len < 12 || get16(d) != query_id || !(d[2] & 0x80))
		return;
	if (d[3] & 0x0f) {		/* error: no such name, ... */
		done = -1;
		return;
	}
	qd = get16(d + 4);
	an = get16(d + 6);
	i = 12;
	while (qd--)
		i = skip_name(d, len, i) + 4;
	while (an-- && i < len) {
		int type, rdlen;

		i = skip_name(d, len, i);
		if (i + 10 > len)
			break;
		type = get16(d + i);
		rdlen = get16(d + i + 8);
		i += 10;
		if (type == 1 && rdlen == 4 && i + 4 <= len) {
			answer = get32(d + i);
			done = 1;
			return;
		}
		i += rdlen;
	}
	done = -1;
}

int dns_resolve(const char *name, u32 *ip)
{
	u8 q[300];
	int n = 12, try;
	const char *s = name;

	if (ip_parse(name, ip))
		return 1;
	if (!nif.up || strlen(name) > 250)
		return 0;
	query_id = ticks * 7 + 1;
	memset(q, 0, 12);
	put16(q, query_id);
	q[2] = 0x01;				/* recursion desired */
	put16(q + 4, 1);
	while (*s) {				/* www.example.com -> 3www7example3com0 */
		int len = 0;

		while (s[len] && s[len] != '.')
			len++;
		if (len == 0 || len > 63)
			return 0;
		q[n++] = len;
		memcpy(q + n, s, len);
		n += len;
		s += len;
		if (*s == '.')
			s++;
	}
	q[n++] = 0;
	put16(q + n, 1);			/* A */
	put16(q + n + 2, 1);			/* IN */
	n += 4;

	udp_bind(DNS_PORT_LOCAL, reply);
	done = 0;
	for (try = 0; try < 3 && !done; try++) {
		u32 start = ticks;

		udp_send(nif.dns, DNS_PORT_LOCAL, 53, q, n);
		while (!done && ticks - start < 120) {
			net_poll();
			cpu_idle();
		}
	}
	udp_unbind(DNS_PORT_LOCAL);
	if (done != 1)
		return 0;
	*ip = answer;
	return 1;
}

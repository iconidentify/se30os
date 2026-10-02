/* telnet.c - a telnet client in a terminal window.
 *
 * We say we are an xterm-256color terminal (TTYPE), report the window
 * size (NAWS), let the server echo, and go 8-bit clean (BINARY) when the
 * server agrees, which is what UTF-8 needs. Ctrl-] closes the session.
 */
#include "kernel.h"
#include "net.h"
#include "telnet.h"

#define IAC	255
#define DONT	254
#define DO	253
#define WONT	252
#define WILL	251
#define SB	250
#define SE	240

#define O_BINARY	0
#define O_ECHO		1
#define O_SGA		3
#define O_TTYPE		24
#define O_NAWS		31

enum { T_DATA, T_IAC, T_OPT, T_SB, T_SB_IAC, T_CR };

struct session {
	struct term *t;
	struct tcp *c;
	int state;
	u8 cmd;
	u8 sb[64];
	int nsb;
	u8 us[256], him[256];		/* options in force */
	void (*old_key)(struct term *, const struct event *);
	void (*old_reply)(struct term *, const char *, int);
	void (*done)(struct term *);
};

static void send(struct session *s, const void *d, int n)
{
	if (s->c)
		tcp_write(s->c, d, n);
}

static void send3(struct session *s, u8 a, u8 b, u8 c)
{
	u8 m[3] = { a, b, c };

	send(s, m, 3);
}

static void send_naws(struct session *s)
{
	u8 m[9] = { IAC, SB, O_NAWS, 0, s->t->cols, 0, s->t->rows, IAC, SE };

	send(s, m, 9);
}

static void negotiate(struct session *s, u8 cmd, u8 opt)
{
	switch (cmd) {
	case DO:
		if (opt == O_TTYPE || opt == O_NAWS || opt == O_BINARY ||
		    opt == O_SGA) {
			if (!s->us[opt]) {
				s->us[opt] = 1;
				send3(s, IAC, WILL, opt);
			}
			if (opt == O_NAWS)
				send_naws(s);
		} else {
			send3(s, IAC, WONT, opt);
		}
		break;
	case DONT:
		if (s->us[opt]) {
			s->us[opt] = 0;
			send3(s, IAC, WONT, opt);
		}
		break;
	case WILL:
		if (opt == O_ECHO || opt == O_SGA || opt == O_BINARY) {
			if (!s->him[opt]) {
				s->him[opt] = 1;
				send3(s, IAC, DO, opt);
			}
		} else {
			send3(s, IAC, DONT, opt);
		}
		break;
	case WONT:
		if (s->him[opt]) {
			s->him[opt] = 0;
			send3(s, IAC, DONT, opt);
		}
		break;
	}
}

static void subneg(struct session *s)
{
	static const char tt[] = "XTERM-256COLOR";

	if (s->nsb >= 2 && s->sb[0] == O_TTYPE && s->sb[1] == 1) {	/* SEND */
		u8 m[4 + sizeof(tt) + 2];
		int n = 0;

		m[n++] = IAC;
		m[n++] = SB;
		m[n++] = O_TTYPE;
		m[n++] = 0;			/* IS */
		memcpy(m + n, tt, sizeof(tt) - 1);
		n += sizeof(tt) - 1;
		m[n++] = IAC;
		m[n++] = SE;
		send(s, m, n);
	}
}

static void recv(struct tcp *c, const u8 *d, int len)
{
	struct session *s = tcp_arg(c);
	char out[256];
	int n = 0;

	while (len--) {
		u8 b = *d++;

		switch (s->state) {
		case T_DATA:
			if (b == IAC) {
				s->state = T_IAC;
				break;
			}
			out[n++] = b;
			if (b == '\r' && !s->him[O_BINARY])
				s->state = T_CR;
			break;
		case T_CR:			/* CR NUL means a bare CR */
			s->state = T_DATA;
			if (b == 0)
				break;
			if (b == IAC) {
				s->state = T_IAC;
				break;
			}
			out[n++] = b;
			break;
		case T_IAC:
			if (b == IAC) {
				out[n++] = b;
				s->state = T_DATA;
			} else if (b >= WILL && b <= DONT) {
				s->cmd = b;
				s->state = T_OPT;
			} else if (b == SB) {
				s->nsb = 0;
				s->state = T_SB;
			} else {
				s->state = T_DATA;	/* NOP, GA, ... */
			}
			break;
		case T_OPT:
			negotiate(s, s->cmd, b);
			s->state = T_DATA;
			break;
		case T_SB:
			if (b == IAC)
				s->state = T_SB_IAC;
			else if (s->nsb < (int)sizeof(s->sb))
				s->sb[s->nsb++] = b;
			break;
		case T_SB_IAC:
			if (b == SE) {
				subneg(s);
				s->state = T_DATA;
			} else {
				if (s->nsb < (int)sizeof(s->sb))
					s->sb[s->nsb++] = b;
				s->state = T_SB;
			}
			break;
		}
		if (n == sizeof(out)) {
			term_write(s->t, out, n);
			n = 0;
		}
	}
	if (n)
		term_write(s->t, out, n);
}

static void finish(struct session *s, const char *why)
{
	struct term *t = s->t;

	t->key = s->old_key;
	t->reply = s->old_reply;
	t->session = NULL;
	t->session_close = NULL;
	t->session_resized = NULL;
	if (why)
		term_printf(t, "\r\n%s\n", why);
	if (s->done)
		s->done(t);
	kfree(s);
}

static void event(struct tcp *c, int ev)
{
	struct session *s = tcp_arg(c);

	switch (ev) {
	case TCP_CONNECTED:
		term_printf(s->t, "Connected. Escape character is '^]'.\n");
		return;
	case TCP_CLOSED:
		tcp_close(c);
		s->c = NULL;
		finish(s, "Connection closed by foreign host.");
		return;
	case TCP_RESET:
		s->c = NULL;
		finish(s, "Connection reset.");
		return;
	case TCP_REFUSED:
		s->c = NULL;
		finish(s, "Connection refused.");
		return;
	case TCP_TIMEOUT:
		s->c = NULL;
		finish(s, "Connection timed out.");
		return;
	}
}

static void key(struct term *t, const struct event *e)
{
	struct session *s = t->session;
	char buf[16];
	int n;

	if (e->ch == 0x1d) {			/* ^] */
		if (s->c)
			tcp_close(s->c);
		s->c = NULL;
		finish(s, "Connection closed.");
		return;
	}
	if (!s->c)
		return;
	if (e->ch == '\r') {
		if (s->us[O_BINARY])
			send(s, "\r", 1);
		else
			send(s, "\r\0", 2);
		return;
	}
	n = term_keyseq(t, e, buf);
	if (n == 1 && (u8)buf[0] == IAC)
		buf[n++] = (char)IAC;
	if (n)
		send(s, buf, n);
}

/* Answers to the terminal's status queries go to the server. */
static void reply(struct term *t, const char *d, int n)
{
	send(t->session, d, n);
}

/* The window changed size: tell the server, if it asked to know. */
static void window_resized(struct term *t)
{
	struct session *s = t->session;

	if (s->us[O_NAWS])
		send_naws(s);
}

/* The window is closing under us: hang up without telling anyone. */
static void window_closing(struct term *t)
{
	struct session *s = t->session;

	if (s->c)
		tcp_abort(s->c);
	kfree(s);
	t->session = NULL;
}

void telnet_open(struct term *t, const char *host, u16 port,
		 void (*done)(struct term *))
{
	struct session *s = kzalloc(sizeof(*s));
	char ipbuf[16];
	u32 ip;

	if (!s)
		return;
	s->t = t;
	s->done = done;
	if (!nif.up) {
		term_printf(t, "telnet: no network\n");
		kfree(s);
		if (done)
			done(t);
		return;
	}
	if (!dns_resolve(host, &ip)) {
		term_printf(t, "telnet: can't find %s\n", host);
		kfree(s);
		if (done)
			done(t);
		return;
	}
	term_printf(t, "Trying %s...\n", ip_str(ip, ipbuf));
	s->old_key = t->key;
	s->old_reply = t->reply;
	t->key = key;
	t->reply = reply;
	t->session = s;
	t->session_close = window_closing;
	t->session_resized = window_resized;
	s->c = tcp_connect(ip, port, recv, event, s);
	if (!s->c)
		finish(s, "telnet: no free connections");
}

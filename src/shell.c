/* shell.c - the built-in command shell, one per terminal window. */
#include "kernel.h"
#include "term.h"
#include "shell.h"
#include "net.h"
#include "nubus.h"
#include "telnet.h"
#include "saver.h"

struct shell {
	struct term *t;
	char buf[120];
	int len;
	int busy;			/* a command still running (telnet) */
};

static void prompt(struct term *t);

struct cmd {
	const char *name, *help;
	void (*fn)(struct term *t, int argc, char **argv);
};

static const struct cmd cmds[];

/* ---- benchmarks ---- */

/* Run fn as many times as fits in 60 ticks (one second) and return the
 * count. */
static u32 per_second(void (*fn)(void *), void *arg)
{
	u32 start = ticks, n = 0;

	while (ticks == start)
		;
	start = ticks;
	while (ticks - start < 60) {
		fn(arg);
		n++;
	}
	return n;
}

static void b_loop(void *arg)
{
	volatile int i;

	for (i = 0; i < 1000; i++)
		;
}

static u8 *bufa, *bufb;

static void b_memcpy(void *arg)
{
	memcpy(bufa, bufb, 65536);
}

static struct bitmap bma, bmb;

static void b_fill(void *arg)
{
	struct rect r = { 0, 0, 512, 342 };

	fill_rect(&bma, &r, pat_gray, M_COPY, NULL);
}

static void b_blit(void *arg)
{
	struct rect r = { 0, 0, 512, 342 };

	copy_bits(&bma, &r, &bmb, 0, 0, M_COPY, NULL);
}

static void b_blit3(void *arg)
{
	struct rect r = { 0, 0, 509, 342 };

	copy_bits(&bma, &r, &bmb, 3, 0, M_COPY, NULL);
}

static void b_screen(void *arg)
{
	struct rect r = { 0, 20, 512, 342 };

	cursor_shield(0, 20, 512, 342);
	copy_bits(&bma, &r, &screen, 0, 20, M_COPY, NULL);
	cursor_unshield();
}

static void b_text(void *arg)
{
	struct term *t = arg;

	term_redraw_all(t);
	t->w->render(t->w);
}

static void b_scroll(void *arg)
{
	struct term *t = arg;

	term_puts(t, "\nThe quick brown fox jumps over the lazy dog 0123456789");
	t->w->render(t->w);
	wm_flush();
}

/* Benchmark results go to the window and the serial port. */
static void report(struct term *t, const char *fmt, ...)
{
	char buf[100];
	va_list ap;

	va_start(ap, fmt);
	kvsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	term_puts(t, buf);
	kprintf("%s", buf);
}

static void c_bench(struct term *t, int argc, char **argv)
{
	u32 n;

	if (!bufa) {
		bufa = kmalloc(65536);
		bufb = kmalloc(65536);
		bitmap_alloc(&bma, 512, 342);
		bitmap_alloc(&bmb, 512, 342);
	}
	n = per_second(b_loop, NULL);
	report(t, "empty loop        %6u K iterations/s\n", n);
	n = per_second(b_memcpy, NULL);
	report(t, "memcpy 64K        %6u KB/s\n", n * 64);
	n = per_second(b_fill, NULL);
	report(t, "fill 512x342      %6u /s  (%u KB/s)\n", n, n * 21888 / 1024);
	n = per_second(b_blit, NULL);
	report(t, "blit aligned      %6u /s  (%u KB/s)\n", n, n * 21888 / 1024);
	n = per_second(b_blit3, NULL);
	report(t, "blit shifted      %6u /s  (%u KB/s)\n", n, n * 21888 / 1024);
	n = per_second(b_screen, NULL);
	report(t, "blit to screen    %6u /s\n", n);
	n = per_second(b_text, t);
	report(t, "draw %ux%u text   %6u screens/s (%u chars/s)\n", t->cols,
		    t->rows, n, n * t->cols * t->rows);
	n = per_second(b_scroll, t);
	report(t, "\nscroll + flush    %6u lines/s\n", n);
	wm_redraw();
}

/* ---- commands ---- */

static void c_help(struct term *t, int argc, char **argv)
{
	const struct cmd *c;

	for (c = cmds; c->name; c++)
		term_printf(t, "  %-8s %s\n", c->name, c->help);
}

static void c_about(struct term *t, int argc, char **argv)
{
	term_printf(t, "se30os - an operating system for the Macintosh SE/30\n");
	term_printf(t, "%u KB RAM, %ux%u screen, 68030 + 68882\n",
		    mach.mem_size / 1024, mach.width, mach.height);
}

static void c_mem(struct term *t, int argc, char **argv)
{
	term_printf(t, "%u KB free of %u KB\n", mem_free() / 1024,
		    mach.mem_size / 1024);
}

static void c_uptime(struct term *t, int argc, char **argv)
{
	u32 s = ticks * 1000 / 60150;

	term_printf(t, "up %u:%02u:%02u (%u ticks)\n", s / 3600, s / 60 % 60,
		    s % 60, ticks);
}

static void c_clear(struct term *t, int argc, char **argv)
{
	term_puts(t, "\033[H\033[2J");
}

static void c_echo(struct term *t, int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++)
		term_printf(t, "%s%s", argv[i], i + 1 < argc ? " " : "");
	term_puts(t, "\n");
}

static void c_cache(struct term *t, int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "off")) {
		cache_off();
		term_puts(t, "caches off\n");
	} else {
		cache_on();
		term_puts(t, "caches on\n");
	}
}

static void c_new(struct term *t, int argc, char **argv)
{
	shell_new();
}

/* Exercise the terminal: UTF-8, box drawing, colours, attributes. */
static void c_termtest(struct term *t, int argc, char **argv)
{
	term_puts(t,
		"\033[1mBold\033[0m \033[4mUnderline\033[0m \033[7mReverse\033[0m "
		"\033[9mStrike\033[0m \033[1;4;7mAll\033[0m\n"
		"\033[41m red bg \033[42m green bg \033[43m yellow bg \033[44m blue bg "
		"\033[47m white bg \033[107m bright white \033[0m\n"
		"\033[48;5;236m 256:236 \033[48;5;250m 256:250 \033[48;2;0;0;128m rgb navy "
		"\033[48;2;255;255;255m rgb white \033[0m \033[31mred fg\033[0m\n"
		"\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90 "
		"\xe2\x95\x94\xe2\x95\x90\xe2\x95\x97 \xe2\x96\x88\xe2\x96\x93\xe2\x96\x92\xe2\x96\x91 "
		"\xe2\x86\x90\xe2\x86\x91\xe2\x86\x92\xe2\x86\x93 \xce\xbb\xcf\x80\xce\xa3 "
		"\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 caf\xc3\xa9 \xe2\x9c\x93\xe2\x9c\x97 "
		"\xe2\xa3\xbf\xe2\xa0\x81 \xe6\x97\xa5\xe6\x9c\xac|\n"
		"\xe2\x94\x82" "ab\xe2\x94\x82" "cd\xe2\x94\x82 \033(0lqqkx  xmqqj\033(B DEC graphics\n"
		"\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98 "
		"insert: ABCDEF\033[3D\033[2@xy  delete: 123456\033[4D\033[2P\n");
}

/* ---- network ---- */

/* Keep the network and the screen going while a command waits. */
static void pump(void)
{
	net_poll();
	wm_flush();
	cpu_idle();
}

static void tprint(void *arg, const char *fmt, ...)
{
	char buf[160];
	va_list ap;

	va_start(ap, fmt);
	kvsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	term_puts(arg, buf);
}

static void c_ip(struct term *t, int argc, char **argv)
{
	char a[16], b[16], c[16], d[16];
	u32 *f[4] = { &nif.ip, &nif.mask, &nif.gw, &nif.dns };
	int i;

	for (i = 1; i < argc && i <= 4; i++) {
		if (!ip_parse(argv[i], f[i - 1])) {
			term_printf(t, "ip: bad address %s\n", argv[i]);
			return;
		}
	}
	term_printf(t, "address %s  mask %s  gateway %s  dns %s\n",
		    ip_str(nif.ip, a), ip_str(nif.mask, b), ip_str(nif.gw, c),
		    ip_str(nif.dns, d));
	if (argc == 1)
		term_puts(t, "set with: ip ADDRESS [MASK [GATEWAY [DNS]]]\n");
}

static void c_net(struct term *t, int argc, char **argv)
{
	if (argc > 1 && !strncmp(argv[1], "test", 4)) {
		/* net test [test32]: card RAM writes and one test transmit */
		ne_test(!strcmp(argv[1], "test32"), nif.ip, nif.gw, tprint, t);
		return;
	}
	if (!nif.up) {
		term_puts(t, "no Ethernet card found\n");
		return;
	}
	term_printf(t, "Ethernet in slot %x, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		    ne_slot(), nif.mac[0], nif.mac[1], nif.mac[2], nif.mac[3],
		    nif.mac[4], nif.mac[5]);
	term_printf(t, "rx %u  tx %u  irqs %u  rx errors %u  tx errors %u\n",
		    ne_stats.rx_packets, ne_stats.tx_packets, ne_stats.irqs,
		    ne_stats.rx_errors, ne_stats.tx_errors);
	term_printf(t, "dropped %u  overruns %u  tx timeouts %u\n",
		    ne_stats.rx_dropped, ne_stats.overruns, ne_stats.tx_timeouts);
	tcp_list(tprint, t);
}

static void c_nubus(struct term *t, int argc, char **argv)
{
	nubus_list(tprint, t);
}

static void c_host(struct term *t, int argc, char **argv)
{
	char a[16];
	u32 ip;

	if (argc < 2) {
		term_puts(t, "usage: host NAME\n");
		return;
	}
	if (dns_resolve(argv[1], &ip))
		term_printf(t, "%s is %s\n", argv[1], ip_str(ip, a));
	else
		term_printf(t, "%s: not found\n", argv[1]);
}

static void c_ping(struct term *t, int argc, char **argv)
{
	char a[16];
	u32 ip;
	int seq, count = argc > 2 ? argv[2][0] - '0' : 4, got = 0;

	if (argc < 2) {
		term_puts(t, "usage: ping HOST [COUNT]\n");
		return;
	}
	if (!nif.up || !dns_resolve(argv[1], &ip)) {
		term_printf(t, "ping: can't reach %s\n", argv[1]);
		return;
	}
	if (count < 1 || count > 9)
		count = 4;
	for (seq = 1; seq <= count; seq++) {
		u32 start = ticks;

		ping_reply_seq = 0;
		icmp_ping(ip, 0x5330, seq);
		while (ping_reply_seq != seq && ticks - start < 60)
			pump();
		if (ping_reply_seq == seq) {
			got++;
			term_printf(t, "reply from %s: seq %d time %u ms\n",
				    ip_str(ip, a), seq,
				    (ping_reply_ticks - start) * 1000 / 60);
		} else {
			term_printf(t, "seq %d: no reply\n", seq);
		}
		wm_flush();
		while (ticks - start < 60)
			pump();
	}
	term_printf(t, "%d of %d replied\n", got, count);
}

static void telnet_done(struct term *t)
{
	struct shell *sh = t->data;

	sh->busy = 0;
	prompt(t);
}

static void c_telnet(struct term *t, int argc, char **argv)
{
	struct shell *sh = t->data;
	int port = 23;

	if (argc < 2) {
		term_puts(t, "usage: telnet HOST [PORT]\n");
		return;
	}
	if (argc > 2) {
		const char *p = argv[2];

		port = 0;
		while (*p >= '0' && *p <= '9')
			port = port * 10 + *p++ - '0';
	}
	sh->busy = 1;
	telnet_open(t, argv[1], port, telnet_done);
}

/* ---- screen savers ---- */

static void say_term(void *arg, const char *s)
{
	term_puts(arg, s);
	wm_flush();			/* show progress as it goes */
}

static void c_saver(struct term *t, int argc, char **argv)
{
	struct saver_stats st;
	int effect = SAVER_CYCLE, seconds = 0, fps = 60, i;

	if (argc > 1 && !strcmp(argv[1], "list")) {
		term_puts(t, "effects:");
		for (i = 0; i < 10; i++)
			term_printf(t, " %s", saver_name(i));
		term_puts(t, "\nusage: saver [EFFECT|cycle] [SECONDS] [FPS]\n");
		return;
	}
	if (argc > 1 && !strcmp(argv[1], "test")) {
		u32 t0 = ticks;

		term_puts(t, "testing the 68k engines (minutes on a 68030)...\n");
		wm_flush();
		i = saver_selftest(say_term, t);
		term_printf(t, "saver self-test %s in %u s\n", i ? "FAILED" : "passed",
			    (ticks - t0) / 60);
		return;
	}
	if (argc > 1) {
		effect = saver_parse(argv[1]);
		if (effect < SAVER_CYCLE) {
			term_printf(t, "saver: no effect %s (try saver list)\n", argv[1]);
			return;
		}
	}
	if (argc > 2) {
		const char *p = argv[2];

		for (seconds = 0; *p >= '0' && *p <= '9'; p++)
			seconds = seconds * 10 + *p - '0';
	}
	if (argc > 3) {
		const char *p = argv[3];

		for (fps = 0; *p >= '0' && *p <= '9'; p++)
			fps = fps * 10 + *p - '0';
	}
	if (saver_run(effect, seconds, fps, &st) < 0) {
		term_puts(t, "saver: needs the SE/30's own 512x342 screen\n");
		return;
	}
	term_printf(t, "%s: %u frames, %u glyphs, %u reveals in %u.%02u s\n",
		    saver_name(effect), st.frames, st.glyphs, st.reveals,
		    st.ticks / 60, st.ticks % 60 * 100 / 60);
}

static void c_adb(struct term *t, int argc, char **argv)
{
	term_printf(t, "%u ADB interrupts, %u packets\n", adb_irqs, adb_packets);
}

static const struct cmd cmds[] = {
	{ "help", "list commands", c_help },
	{ "about", "about this machine", c_about },
	{ "bench", "measure drawing speed", c_bench },
	{ "mem", "free memory", c_mem },
	{ "uptime", "time since boot", c_uptime },
	{ "clear", "clear the window", c_clear },
	{ "echo", "print the arguments", c_echo },
	{ "cache", "cache on|off: CPU caches", c_cache },
	{ "new", "open another terminal", c_new },
	{ "adb", "ADB statistics", c_adb },
	{ "termtest", "show what the terminal can draw", c_termtest },
	{ "saver", "Omarchy screen savers: saver [EFFECT] [SECS]; list; test", c_saver },
	{ "telnet", "telnet HOST [PORT]: log in elsewhere", c_telnet },
	{ "ping", "ping HOST [COUNT]", c_ping },
	{ "host", "host NAME: look up an address", c_host },
	{ "ip", "show or set the IP configuration", c_ip },
	{ "net", "Ethernet card and connections; net test", c_net },
	{ "nubus", "list the cards in the slots", c_nubus },
	{ NULL, NULL, NULL },
};

static void run(struct term *t, char *line)
{
	char *argv[16];
	int argc = 0;
	const struct cmd *c;

	while (*line && argc < 16) {
		while (*line == ' ')
			*line++ = 0;
		if (!*line)
			break;
		argv[argc++] = line;
		while (*line && *line != ' ')
			line++;
	}
	if (!argc)
		return;
	for (c = cmds; c->name; c++) {
		if (!strcmp(c->name, argv[0])) {
			c->fn(t, argc, argv);
			return;
		}
	}
	term_printf(t, "%s: unknown command (try help)\n", argv[0]);
}

static void prompt(struct term *t)
{
	term_puts(t, "se30> ");
}

static void input(struct term *t, const struct event *e)
{
	struct shell *sh = t->data;
	int ch = e->ch;

	if (ch == '\r') {
		term_puts(t, "\n");
		sh->buf[sh->len] = 0;
		sh->len = 0;
		run(t, sh->buf);
		if (!sh->busy)
			prompt(t);
	} else if (ch == '\b' || ch == 0x7f) {
		if (sh->len) {
			sh->len--;
			term_puts(t, "\b \b");
		}
	} else if (ch == 0x15) {		/* ^U: kill the line */
		while (sh->len) {
			sh->len--;
			term_puts(t, "\b \b");
		}
	} else if (ch >= ' ' && ch < 0x7f && sh->len < (int)sizeof(sh->buf) - 1) {
		sh->buf[sh->len++] = ch;
		term_putc(t, ch);
	}
}

void shell_new(void)
{
	static int n;
	struct shell *sh = kzalloc(sizeof(*sh));
	struct term *t;
	char title[16];
	int off = (n % 5) * 12;

	ksnprintf(title, sizeof(title), n ? "Terminal %d" : "Terminal", n + 1);
	t = term_new(8 + off, 27 + off, 80, 22, title);
	if (!t) {
		kfree(sh);
		return;
	}
	n++;
	sh->t = t;
	t->data = sh;
	t->key = input;
	term_puts(t, "se30os on a Macintosh SE/30. Type help for commands.\n");
	prompt(t);
}

/* main.c - kernel entry, exceptions and interrupt dispatch. */
#include "kernel.h"
#include "input.h"
#include "wm.h"
#include "shell.h"
#include "net.h"
#include "saver.h"

extern void exc_common(void);
extern u32 entry_load, entry_cacr, entry_vbr, entry_tc, entry_d0;
extern int bootinfo_version;
extern u16 entry_sr;

static const char *stage = "start";	/* what booting was doing */

/* The CPU's exception frame: SR, PC, then format and vector offset. */
struct frame {
	u16 sr;
	u32 pc;
	u16 vec;
} __attribute__((packed));

static void (*vectors[256])(void) __attribute__((aligned(4)));

volatile u32 ticks;

static void via1_irq(void)
{
	volatile u8 *via = mach.via1;
	u8 flags = via[VIA_IFR] & via[VIA_IER] & 0x7f;

	if (flags & VIA_INT_SR) {
		via[VIA_IFR] = VIA_INT_SR;
		adb_irq();
	}
	if (flags & VIA_INT_CA1) {
		via[VIA_IFR] = VIA_INT_CA1;
		ticks++;
		input_tick();
	}
	flags &= ~(VIA_INT_CA1 | VIA_INT_SR);
	if (flags)
		via[VIA_IFR] = flags;	/* nothing else is ours yet */
}

/* VIA2: slot interrupts arrive on CA1; port A says which slot (low). */
static void via2_irq(void)
{
	volatile u8 *via = mach.via2;
	u8 flags = via[VIA_IFR] & 0x7f;

	via[VIA_IFR] = flags;
	if (flags & VIA_INT_CA1) {
		int slot = ne_slot();

		if (slot && !(via[VIA_A] & (1 << (slot - 9))))
			ne_irq();
	}
}

static const char *exc_name(unsigned v)
{
	switch (v) {
	case 2: return "bus error";
	case 3: return "address error";
	case 4: return "illegal instruction";
	case 5: return "divide by zero";
	case 8: return "privilege violation";
	case 10: return "line A";
	case 11: return "line F";
	case 14: return "format error";
	case 56: return "MMU configuration error";
	}
	return v >= 24 && v < 32 ? "interrupt" : "exception";
}

void exception(struct frame *f)
{
	unsigned v = (f->vec & 0x0fff) >> 2, fmt = f->vec >> 12;
	const u8 *raw = (const u8 *)f;

	switch (v) {
	case 25:			/* level 1: VIA1 */
		via1_irq();
		return;
	case 26:			/* level 2: VIA2 */
		via2_irq();
		return;
	case 24:			/* spurious */
		return;
	}
	/* Anything else is fatal: say so on the screen, whatever is there.
	   The MMU and data cache go off first, so a bad mapping can't stop
	   the report from reaching the screen. */
	irq_off();
	mmu_off();
	cache_off();
	if (!console_ready) {
		screen_init();
		console_ready = 1;
	}
	kprintf("\n*** se30os stopped: %s (vector %u) during %s\n", exc_name(v), v,
		stage);
	kprintf("pc %p  sr %04x  frame format %x", f->pc, f->sr, fmt);
	if (fmt == 0xa || fmt == 0xb)
		kprintf("  fault address %p  ssw %04x", *(const u32 *)(raw + 16),
			*(const u16 *)(raw + 10));
	kprintf("\n");
	for (;;)
		;
}

static void vectors_init(void)
{
	int i;

	for (i = 0; i < 256; i++)
		vectors[i] = exc_common;
	__asm__ volatile("movec %0,%%vbr" :: "r"(vectors));
}

static void via_init(void)
{
	/* Mac OS left its interrupts set up; turn everything off first. */
	mach.via1[VIA_IER] = 0x7f;
	mach.via1[VIA_IFR] = 0x7f;
	mach.via2[VIA_IER] = 0x7f;
	mach.via2[VIA_IFR] = 0x7f;
	/* VIA1 CA1: 60.15 Hz vertical blanking */
	mach.via1[VIA_IER] = 0x80 | VIA_INT_CA1;
	/* SE/30: keep the built-in video's slot interrupt quiet */
	if (mach.model == 9) {
		mach.via1[VIA_DDRB] |= 0x40;
		mach.via1[VIA_B] |= 0x40;
	}
	/* VIA2 PB1 high unlocks NuBus/PDS transactions; CA1 is the slots'
	   shared interrupt */
	mach.via2[VIA_DDRB] |= 0x02;
	mach.via2[VIA_B] |= 0x02;
	mach.via2[VIA_IER] = 0x80 | VIA_INT_CA1;
}

static void about(void)
{
	struct window *w = win_new(136, 90, 240, 100, "About se30os");
	char buf[64];
	int x, y;

	if (!w)
		return;
	x = w->content.left + 20;
	y = w->content.top;

	draw_text(&w->bm, x, y + 20, "se30os", &font_6x13, M_OR, NULL);
	draw_text(&w->bm, x + 1, y + 20, "se30os", &font_6x13, M_OR, NULL);
	draw_text(&w->bm, x, y + 40, "for the Macintosh SE/30", &font_6x13, M_OR, NULL);
	ksnprintf(buf, sizeof(buf), "%u KB free", mem_free() / 1024);
	draw_text(&w->bm, x, y + 60, buf, &font_6x13, M_OR, NULL);
	win_invalidate(w, &w->content);
}

static void close_front(void)
{
	if (win_frontmost())
		win_close(win_frontmost());
}

static void zoom_front(void)
{
	struct window *w = win_frontmost();

	if (w && w->resized)
		win_zoom(w);
}

static const struct menu_item window_menu[] = {
	{ "Full Screen", 'F', zoom_front },
};

static void screen_saver(void)
{
	struct saver_stats st;

	saver_run(SAVER_CYCLE, 0, 60, &st);
}

static const struct menu_item sys_menu[] = {
	{ "About se30os...", 0, about },
	{ "Screen Saver", 'S', screen_saver },
};

static const struct menu_item file_menu[] = {
	{ "New Terminal", 'N', shell_new },
	{ "Close", 'W', close_front },
};

/* Announce a step of booting, so a hang shows where it happened. */
static void step(const char *what)
{
	stage = what;
	kprintf("%s... ", what);
}

static void boot_log(void)
{
	int i;

	kprintf("se30os booting\n");
	kprintf("loaded at %p (linked at 0x1000); Penguin left sr %04x cacr %x vbr %p tc %p\n",
		entry_load, entry_sr, entry_cacr, entry_vbr, entry_tc);
	kprintf("bootinfo version %d, Penguin's tag %c%c%c%c\n", bootinfo_version,
		entry_d0 >> 24 >= ' ' && entry_d0 >> 24 < 127 ? entry_d0 >> 24 : '?',
		(entry_d0 >> 16 & 255) >= ' ' && (entry_d0 >> 16 & 255) < 127 ? entry_d0 >> 16 & 255 : '?',
		(entry_d0 >> 8 & 255) >= ' ' && (entry_d0 >> 8 & 255) < 127 ? entry_d0 >> 8 & 255 : '?',
		(entry_d0 & 255) >= ' ' && (entry_d0 & 255) < 127 ? entry_d0 & 255 : '?');
	kprintf("model %u cpu %x fpu %x mmu %x\n", mach.model, mach.cpu, mach.fpu, mach.mmu);
	for (i = 0; i < mach.nchunks; i++)
		kprintf("memory %p + %u KB\n", mach.chunk[i].addr, mach.chunk[i].size / 1024);
	kprintf("video %p %ux%u depth %u rowbytes %u\n", (u32)mach.video, mach.width,
		mach.height, mach.depth, mach.rowbytes);
	kprintf("via1 %p via2 %p scc %p rom %p\n", (u32)mach.via1, (u32)mach.via2,
		(u32)mach.scc, mach.rombase);
	kprintf("command line: %s\n", mach.cmdline);
}

/* Busy-wait about n milliseconds, before the tick interrupt runs. */
static void spin_ms(int n)
{
	volatile int i;

	while (n--)
		for (i = 0; i < 400; i++)
			;
}

void kmain(const void *bootinfo)
{
	struct event e;
	u32 t;

	bootinfo_parse(bootinfo);
	/* The screen first: if anything goes wrong, it says what. */
	if (!mach.video || mach.depth != 1 || mach.width != 512) {
		mach.video = (u8 *)0xfe008040;
		mach.width = 512;
		mach.height = 342;
		mach.rowbytes = 64;
		mach.depth = 1;
	}
	screen_init();
	console_ready = 1;
	vectors_init();
	boot_log();

	step("serial");
	serial_init();
	kprintf("ok\n");
	boot_log();			/* again, now that serial is up */

	step("mmu");
	if (boot_option("nommu")) {
		kprintf("skipped\n");
	} else {
		mmu_init();
		kprintf("ok\n");
		step("caches");
		if (boot_option("nocache")) {
			kprintf("skipped\n");
		} else {
			cache_on();
			kprintf("ok\n");
		}
	}
	step("heap");
	mem_init();
	kprintf("%u KB\n", mem_free() / 1024);
	step("via");
	via_init();
	kprintf("ok\n");
	step("graphics");
	gfx_init();
	cursor_init();
	kprintf("ok\n");
	step("adb");
	if (boot_option("noadb")) {
		kprintf("skipped\n");
	} else {
		adb_init();
		kprintf("ok\n");
	}
	step("interrupts");
	irq_on();
	t = ticks;
	spin_ms(200);
	if (ticks == t)
		kprintf("no vertical blank interrupts!\n");
	else
		kprintf("ok, %u ticks in 0.2 s\n", ticks - t);
	step("ethernet");
	if (boot_option("nonet")) {
		kprintf("skipped\n");
	} else {
		net_init();
		if (nif.up)
			kprintf("slot %x, mac %02x:%02x:%02x:%02x:%02x:%02x\n", ne_slot(),
				nif.mac[0], nif.mac[1], nif.mac[2], nif.mac[3],
				nif.mac[4], nif.mac[5]);
		else
			kprintf("no card\n");
	}

	/* Leave the log up long enough to read (or photograph). */
	kprintf("starting the desktop%s\n", boot_option("wait") ? " in 15 s" : "");
	step("desktop");
	t = ticks;
	if (ticks)
		while (ticks - t < (boot_option("wait") ? 900u : 90u))
			cpu_idle();
	else
		spin_ms(boot_option("wait") ? 15000 : 1500);
	console_ready = 0;
	wm_add_menu("se30os", sys_menu, 2);
	wm_add_menu("File", file_menu, 2);
	wm_add_menu("Window", window_menu, 1);
	wm_init();
	shell_new();
	stage = "running";
	kprintf("desktop up\n");

	for (;;) {
		net_poll();
		while (event_get(&e))
			wm_event(&e);
		wm_flush();
		irq_off();
		if (event_pending())
			irq_on();
		else
			cpu_idle();	/* enables interrupts as it stops */
	}
}

# se30os

A small operating system written from scratch for the Macintosh SE/30, in C and 68k assembly. The goal is a fast Mac-style desktop where the mouse never lags.

- **Where it runs.** It runs on a real SE/30 (68 MB, a 32-bit-clean ROM, a SCSI2SD and an Asante MacCon 30si), and is developed and tested in MAME's `macse30`.
- **Booting.** It boots from [se30boot](https://github.com/iconidentify/se30boot), a boot menu that doesn't need Mac OS, or from Penguin like a Linux kernel.
- **Companion project.** [se30-linux](https://github.com/iconidentify/se30-linux) is Debian m68k with Linux 7.2.8, tuned for the same machine.

## What works

- **Boot.** se30boot or Penguin loads it like a Linux kernel and passes the machine details through bootinfo.
- **MMU and caches.** The MMU is on with an identity map. RAM and ROM are cached; I/O and video are not.
- **ADB keyboard and mouse.** They are interrupt driven, with key repeat.
- **Mouse pointer.** It is redrawn inside the ADB interrupt as soon as a mouse packet arrives, so its latency does not depend on what the rest of the system is doing.
- **Window manager.**
  - Menu bar and pull-down menus with ⌘-key shortcuts.
  - Overlapping windows with a close box.
  - Drag by the title bar.
  - Every window keeps its own off-screen bitmap, so exposing or moving a window is just copying.
- **Terminal windows.**
  - Text is assembled 16 characters (three longs) at a time in assembly.
  - Bursts of output collapse to one redraw per frame.
- **Built-in shell.** Commands: `help`, `about`, `bench`, `mem`, `uptime`, `clear`, `echo`, `cache on|off`, `new`, `adb`, `termtest`, plus the network commands below.
- **xterm-256color terminal.**
  - UTF-8 with all 4,121 glyphs of the 6x13 Unicode font: box drawing, blocks, braille, Greek, Cyrillic and arrows.
  - Double-width characters take two cells.
  - Bold, underline, reverse and strikethrough.
  - Colours become black and white: a background darker than nearly-white is drawn inverted, so highlights and status bars stay visible.
  - Scrolling regions, insert/delete of lines and characters, and the alternate screen.
  - DEC line drawing, tab stops, and cursor save/restore.
  - Answers to status and cursor-position queries; window titles set by programs.
  - xterm key sequences: arrows, Home/End, PgUp/PgDn, F1–F15. Option acts as Meta.
  - htop, vim, less and `ls --color` all render correctly over telnet.
- **Full screen.** ⌘F (Window menu) turns a terminal into an 85×26 screen with no frame; it is 80×22 in a window. A telnet server is told about the new size.
- **Ethernet.** The DP8390 cards: Asante MacCon, Farallon EtherMac, and Apple NuBus Ethernet.
  - Cards are found by reading their NuBus declaration ROMs, safely probing empty slots.
  - The card's interrupt is used, and polling backs it up.
  - Card RAM is only ever accessed 16 bits at a time, which Asante cards need.
- **TCP/IP.** ARP, IPv4, ICMP (it answers pings), UDP, a DNS resolver, and client TCP with retransmission.
- **Telnet client.** Negotiates TTYPE (`XTERM-256COLOR`), NAWS (window size), BINARY, ECHO and SGA. ^] hangs up.

## Screen savers

The ten Omarchy effects from omarchy68k are in `src/omarchy`. omarchy68k is a set of 68k assembly engines written for A/UX on the SE/30, adapted from TerminalTextEffects via omacom/ttfx (MIT; see `src/omarchy/NOTICE`). The only change is the banner's bottom line, which now reads "OMARCHY / SE30OS / MOTOROLA 68030" instead of naming A/UX.

- Like the A/UX direct demo, each frame stamps the changed 6x13 glyphs straight into video RAM, white on black.
- A key, a click or moving the mouse ends it, and the desktop is repainted from the windows' bitmaps. Network connections keep running underneath.
- In MAME, matrix and thunderstorm run exactly the frame counts measured with the A/UX demo on the real SE/30: 322 and 240.

| Command | What it does |
|---|---|
| `saver` | Cycles through all ten effects until woken. The se30os menu's Screen Saver item (⌘S) does the same. |
| `saver EFFECT [SECONDS [FPS]]` | Runs one effect: decrypt, thunderstorm, matrix, spotlights, laseretch, vhstape, fireworks, bouncyballs, expand or beams. The default is 60 fps, until woken. |
| `saver list` | Lists the effects. |
| `saver test` | Runs the engines' own self-test (`effects-test.c`). All eight engines it covers pass, with 0 failures. It takes 16 minutes on a 16 MHz 68030. |

## Network commands

| Command | What it does |
|---|---|
| `ip [ADDRESS [MASK [GATEWAY [DNS]]]]` | Shows or sets the configuration. The default is 10.1.1.214/24, with gateway and DNS 10.1.1.1. |
| `telnet HOST [PORT]` | Logs in to another machine. |
| `ping HOST [COUNT]` | Pings a host. |
| `host NAME` | Looks up a name in DNS. |
| `net` | Shows the card, its MAC address, packet and error counters, and open connections. |
| `nubus` | Lists what the slot cards' declaration ROMs say. |

## Building and testing

- **Build containers.** The build runs in a Debian container with an m68k cross compiler. Make it once:

  ```
  sudo docker build -t se30-build -f docker/Dockerfile.se30-build docker
  sudo docker build -t se30-xbuild -f docker/Dockerfile.se30-xbuild docker
  ```

- **Build.** `./make.sh` builds `build/kernel.gz`.
- **Test in MAME.** `./test/boot.sh [seconds [command ...]]` boots the kernel on MAME's SE/30 and prints the serial log. Some things aren't in this repository, because they are Apple's:
  - **The SE/30 ROM.** MAME's `macse30` ROM set goes in a directory named by `SE30_ROMS`.
  - **A startup disk.** `test/mac-base.img.zst` is a System 7.1 disk image (zstd-compressed) with an Apple partition map and two partitions: the Mac volume (System 7.1 with MODE32) and the Linux root.
    - Its `Linux` folder holds Penguin 17, with saved settings that boot `:Linux:vmlinux.gz`.
    - An alias to Penguin in Startup Items starts it automatically.
    - The test puts our kernel in place of `vmlinux.gz`.
  - **MAME itself.** `test/mame/Dockerfile` makes the `se30-mame` image, and `Dockerfile.net` makes `se30-mame-net` for `NET=1`. `test/mame/pmmu/build.sh` builds `se30-mame-pmmu`, which `test/boot.sh` prefers: MAME with the 68030 MMU's limit checks, which stock MAME lacks (see Notes).
- **The commands** drive the emulated mouse and keyboard, for example `'mouse -50 20 1' 'click' 'type help' 'key return' 'snap name'`. Screenshots go to `test/out/`.
- **`NET=1`** adds a Farallon EtherMac 30i on a TAP interface.
  - MAME needs the card's declaration ROM, so `tools/mkdeclrom.py` builds our own in the documented NuBus format.
  - The host side is 10.0.2.1. It runs inetutils telnetd, with htop and vim installed to try. The packet log goes to `test/out/net.txt`.
  - For example: `NET=1 ./test/boot.sh 90 'type ip 10.0.2.15 255.255.255.0 10.0.2.1' 'key return' 'type telnet 10.0.2.1' 'key return' 'wait 4' 'snap telnet'`.
- **`SE30BOOT=1`** boots through [se30boot](https://github.com/iconidentify/se30boot), checked out and built next to this repository (or in `SE30BOOT_DIR`).
- **`tools/telnet-host`** is a sandboxed Debian telnet server to try the terminal against: `docker build --build-arg PASSWORD=... --build-arg ALLOW="SE30-ADDRESS" tools/telnet-host`.

## Booting on the real SE/30

- **Boot log.** se30os clears the screen and logs each boot step before the desktop appears. If something goes wrong, the last line shows where.
  - The log records where Penguin loaded the kernel, the CPU state it left, and the bootinfo version and tag Penguin sent (`Pen2` for tagged records, `Pen1` for the old flat struct).
  - It also lists every memory chunk, the video, VIA and SCC addresses, and Penguin's command line.
  - Any crash prints the exception, the boot step, the PC and, for bus errors, the fault address.
- **Robust to how Penguin loads it.**
  - Penguin 19 sorts memory chunks largest first and loads the kernel 4 KB into the first one, which need not be the bank at address 0. `start.S` moves the image and bootinfo to 0x1000, where the kernel is linked. Build with `EXTRA=-DTEST_RELOC` to test this in MAME.
  - The old flat bootinfo is understood as well as tagged records.
  - The heap is built from every memory chunk.
- **Boot options.** The kernel command line (se30boot's `append`, or Penguin's command line) can carry these:

  | Option | Effect |
  |---|---|
  | `nommu` | Don't turn on the MMU (and therefore not the data cache). |
  | `nocache` | Leave the data cache off. |
  | `nonet` | Skip the Ethernet card. |
  | `noadb` | Skip the keyboard and mouse. |
  | `wait` | Keep the boot log on screen for 15 s. |

- **`net test`.** Writes patterns into the Ethernet card's transmit buffer with 16- and 8-bit accesses and reads them back. Then it sends one ARP request with interrupts off and prints the chip's transmit status (ISR/TSR). `net test32` adds 32-bit writes, which Linux warns can corrupt memory on some Asante cards.

## Running on the SE/30

- **With se30boot.** Put `build/kernel.gz` on the Mac volume as `:Linux:se30os.gz`. With the card in a reader, `tools/install-card.sh /dev/sdX` does this, and nothing else changes. se30boot's default menu already has an se30os entry.
- **With Penguin.** In Penguin's Settings (⌘T), choose `se30os.gz` as the kernel, then Boot Now (⌘B).

## Benchmarks (MAME, emulated 16 MHz 68030)

The `bench` shell command produces these numbers.

| Test                      | Rate                             |
|---------------------------|----------------------------------|
| memcpy                    | 6.8 MB/s                         |
| Fill, 512×342             | 148/s                            |
| Blit, aligned             | 4.2 MB/s                         |
| Blit, shifted             | 1.8 MB/s                         |
| Draw an 80×22 terminal    | 31 screens/s (55,000 chars/s)    |
| Scroll a line and flush   | 80 lines/s                       |

Window positions snap to 8 pixels horizontally. That way a window-to-screen copy is always a whole number of bytes, which the 68030 handles without any shifting.

## Notes

- **The MMU hang on the real SE/30 (fixed).** se30os set the MMU's root pointer to `0x00000002`, meant as "no limit". On a 68030 that is an upper limit of 0, so every address above 256 MB faulted, from the serial port and screen up. The first `kprintf` after the MMU went on bus-errored, and so did the handler's report, so the SE/30 hung after "mmu...". MAME doesn't check limits, so it booted there.
  - The root pointer is now `0x7fff0002`.
  - Fatal exceptions turn the MMU and data cache off before printing, so a bad mapping can't hide its own report.
  - `test/mame/pmmu/` rebuilds MAME 0.289's SE/30 driver with `pmmu-limits.patch`, which makes it check root-pointer and long-descriptor limits as a real 68030 does. `test/boot.sh` uses that image (`se30-mame-pmmu`) when it exists.
  - On it, the old kernel hangs at "mmu..." exactly as the SE/30 did, and the fixed one boots. Built with `EXTRA=-DTEST_MMU_LIMIT`, the old limit now prints `bus error during mmu, fault address 50f04002` (the serial port).

- **MAME's ADB transceiver** runs Apple's real PIC firmware. It signals an autopoll reply without asserting `/CTLR_IRQ`, because the firmware's RB0/RB1 inputs read low in MAME. The driver therefore recognises an autopoll reply by its first byte, the polled command.
- **MAME's ADB mouse** drops a packet that is identical to the previous one. `test/mame/remote.lua` alternates step sizes to work around this.

## Licence

MIT, see `LICENSE`. `src/omarchy` is MIT, under its own `LICENSE`, `NOTICE` and `BRANDING-LICENSE`. The 6x13 font is public domain. `test/mame/pmmu/pmmu-limits.patch` is BSD-3-Clause, like the MAME file it changes.

# se30os - build with ./make.sh (runs make in the m68k cross-compiler container)
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
CFLAGS  := -m68030 -msoft-float -Os -std=gnu11 -ffreestanding -fno-builtin \
           -fno-pic -fno-pie -fomit-frame-pointer -Wall -Wextra -Wno-unused-parameter \
           $(EXTRA)
LDFLAGS := -nostdlib -static -no-pie -T src/link.ld -Wl,--build-id=none \
           -Wl,-z,max-page-size=0x1000 -Wl,-Map,build/kernel.map

OBJS := $(addprefix build/,start.o main.o bootinfo.o serial.o screen.o printf.o font6x13.o adb.o input.o cursor.o lib.o mmu.o mem.o gfx.o region.o wm.o term.o shell.o fast.o font_uni.o nubus.o ne.o net.o tcp.o dns.o telnet.o saver.o) \
        $(addprefix build/omarchy/,decrypt.o storm.o extras.o motion.o raster.o effects.o effects-test.o)

all: build/kernel.gz

build/kernel.gz: build/kernel.elf
	gzip -9nc $< > $@
	@ls -l $< $@

build/kernel.elf: $(OBJS) src/link.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

build/%.o: src/%.c $(wildcard src/*.h) | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/%.o: src/%.S | build
	$(CC) $(CFLAGS) -c -o $@ $<

# The omarchy68k screen savers, copied unchanged from aux-c89-summer
build/omarchy/%.o: src/omarchy/%.s | build/omarchy
	$(CC) $(CFLAGS) -c -o $@ $<

build/omarchy/%.o: src/omarchy/%.c $(wildcard src/omarchy/*.h) | build/omarchy
	$(CC) $(CFLAGS) -w -Isrc/omarchy/compat -c -o $@ $<

build build/omarchy:
	mkdir -p $@

clean:
	rm -rf build

.PHONY: all clean

# the drawing code is worth the extra size
build/gfx.o build/term.o build/cursor.o: CFLAGS += -O2

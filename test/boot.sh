#!/bin/sh
# boot.sh [seconds [command ...]]: boot build/kernel.gz on MAME's Macintosh
# SE/30, run any remote.lua commands (mouse, click, key, type, snap NAME,
# wait), and print the serial log. The disk is System 7.1 + MODE32 with Penguin set to
# auto-boot :Linux:vmlinux.gz, which we replace with our kernel. A
# screenshot of the SE/30's screen goes to test/out/screen.png.
#   SE30_ROMS   MAME macse30 ROM directory (default ~/mame-mac-roms/raw)
#   SE30_WORK   scratch directory (default ${TMPDIR:-/tmp}/se30os-test)
#   SE30_RAM    MAME RAM size (default 64M; 68M is a 64 MB + 4 MB SE/30)
#   SE30_MAME_IMAGE  the MAME container: se30-mame-pmmu if it exists (MAME
#               rebuilt to enforce the 68030 MMU's limits like real hardware,
#               see ~/source/mame-se30), else the stock se30-mame
#   NET=1       add an Ethernet card (Farallon EtherMac 30i, with our own
#               declaration ROM) on a TAP interface: the host side is
#               10.0.2.1, running a telnet server, and packets are logged
#               to test/out/net.txt
#   SE30BOOT=1  boot through se30boot (github.com/iconidentify/se30boot,
#               checked out and built in SE30BOOT_DIR, default ../se30boot), with
#               build/kernel.gz as :Linux:se30os.gz and test/se30boot-test.cfg
#               (SE30BOOT_CFG=FILE for another, or "default" for the
#               installer's own)
set -e
SECS=${1:-120}
[ $# -gt 0 ] && shift
T=$(dirname "$(readlink -f "$0")")
P=$T/..
ROMS=${SE30_ROMS:-$HOME/mame-mac-roms/raw}
W=${SE30_WORK:-${TMPDIR:-/tmp}/se30os-test}

mkdir -p "$W/ctl" "$W/shots" "$W/roms/macse30" "$W/roms/adbmodem" "$T/out"
cp "$ROMS"/* "$W/roms/macse30/"
cp "$ROMS/342s0440-b.bin" "$W/roms/adbmodem/"
cp -r "$T/mame/nvram" "$W/"
cp "$T/mame/remote.lua" "$T/mame/serialbridge.py" "$T/mame/mc" "$W/"
rm -f "$W/ctl/"* "$W/serial.log" "$W/shots/"*.png "$W/netside.sh"
sudo rm -f "$W/cfg/macse30.cfg" "$W/net.txt"
# MAME rebuilt to check the 68030 MMU's limits, as real hardware does, if built
IMAGE=se30-mame
sudo docker image inspect se30-mame-pmmu >/dev/null 2>&1 && IMAGE=se30-mame-pmmu
IMAGE=${SE30_MAME_IMAGE:-$IMAGE}
DOCKER_NET=
MAME_NET=
if [ "${NET:-0}" = 1 ]; then
    IMAGE=se30-mame-net
    DOCKER_NET="--cap-add NET_ADMIN --device /dev/net/tun"
    MAME_NET="-pds030 ethermac30"
    mkdir -p "$W/roms/pds30_emac"
    # MAME wants the card's declaration ROM: we make our own
    python3 "$P/tools/mkdeclrom.py" "$W/roms/pds30_emac/5000118-00-01.bin"
    sudo mkdir -p "$W/cfg"
    sudo cp "$T/mame/macse30-net.cfg" "$W/cfg/macse30.cfg"
    cp "$T/mame/netside.sh" "$W/"
fi
zstd -dqf "$T/mac-base.img.zst" -o "$W/disk.img"

if [ "${SE30BOOT:-0}" = 1 ]; then
    # se30boot (built in SE30BOOT_DIR, default ../se30boot) in the boot
    # blocks, our kernel as :Linux:se30os.gz next to Linux, and a menu that
    # waits for a key
    SB=${SE30BOOT_DIR:-$P/../se30boot}
    [ -x "$SB/tools/install-se30boot.sh" ] || { echo "no se30boot in $SB (set SE30BOOT_DIR)"; exit 1; }
    sudo docker run --rm -v "$W":/work -v "$P/build":/kbuild -w /work se30-build sh -c '
        hmount disk.img 1 >/dev/null && { hdel :Linux:se30os.gz 2>/dev/null || true; } &&
        hcopy -r /kbuild/kernel.gz :Linux:se30os.gz && humount'
    if [ "$SE30BOOT_CFG" = default ]; then
        env -u SE30BOOT_CFG "$SB/tools/install-se30boot.sh" "$W/disk.img"
    else
        SE30BOOT_CFG=${SE30BOOT_CFG:-$T/se30boot-test.cfg} "$SB/tools/install-se30boot.sh" "$W/disk.img"
    fi
else
    # Put the kernel where Penguin's saved settings point.
    sudo docker run --rm -v "$W":/work -v "$P/build":/kbuild -w /work se30-build sh -c '
        hmount disk.img 1 >/dev/null && hdel :Linux:vmlinux.gz &&
        hcopy -r /kbuild/kernel.gz :Linux:vmlinux.gz && humount'
fi
sudo docker run --rm -v "$W":/work -w /work -e PATH=/usr/games:/usr/bin:/bin se30-mame \
    chdman createhd -i disk.img -o disk.chd -c none -f >/dev/null 2>&1
rm -f "$W/disk.img"

sudo docker rm -f se30os-mame >/dev/null 2>&1 || true
sudo touch "$W/serial.log"
sudo docker run -d --name se30os-mame $DOCKER_NET -v "$W":/work -w /work \
    -e PATH=/usr/games:/usr/sbin:/usr/bin:/sbin:/bin \
    $IMAGE sh -c "python3 /work/serialbridge.py & [ -x /work/netside.sh ] && /work/netside.sh & sleep 1; xvfb-run -a mame macse30 $MAME_NET \
    -rompath /work/roms -ramsize ${SE30_RAM:-64M} -hard disk.chd -modem null_modem -bitb socket.127.0.0.1:2323 \
    -video none -sound none -nothrottle -skip_gameinfo -nvram_directory /work/nvram \
    -cfg_directory /work/cfg -snapshot_directory /work/shots -autoboot_script /work/remote.lua \
    > /work/mame.log 2>&1" >/dev/null
sleep 3
MC_TIMEOUT=900 "$W/mc" "wait $SECS" "snap screen" "$@" >/dev/null
printf 'quit\n' > "$W/ctl/cmd"
for i in $(seq 1 30); do sudo docker ps -q --filter name=se30os-mame | grep -q . || break; sleep 1; done
sudo docker rm -f se30os-mame >/dev/null 2>&1 || true
sudo sh -c "cp \"$W\"/shots/*.png \"$T/out/\" && chown $(id -u) \"$T\"/out/*.png" 2>/dev/null || true
[ "${NET:-0}" = 1 ] && sudo cp "$W/net.txt" "$T/out/net.txt" 2>/dev/null || true
sudo cat "$W/serial.log" | tr -d '\r'

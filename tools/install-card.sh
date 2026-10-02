#!/bin/sh
# install-card.sh /dev/sdX: put build/kernel.gz on the SE/30's disk (the
# first HFS volume, which needs a Linux folder) as :Linux:se30os.gz.
# Nothing else changes. Boot it from se30boot's menu, or in Penguin choose
# se30os.gz as the kernel in Settings and Boot Now.
set -e
DEV=${1:?usage: install-card.sh /dev/sdX}
P=$(dirname "$(readlink -f "$0")")/..
[ -b "$DEV" ] || { echo "$DEV is not a block device"; exit 1; }
[ -f "$P/build/kernel.gz" ] || { echo "build first (./make.sh)"; exit 1; }
# the desktop mounts the card's partitions when it's inserted
for part in "$DEV"?*; do
    if findmnt -n "$part" >/dev/null 2>&1; then udisksctl unmount -b "$part"; fi
done
sudo docker run --rm --device "$DEV" -v "$P/build":/kbuild:ro se30-build sh -c "
    set -e
    hmount $DEV 1
    hls :Linux >/dev/null || { echo 'no Linux folder on the Mac volume'; exit 1; }
    hdel :Linux:se30os.gz 2>/dev/null || true
    hcopy -r /kbuild/kernel.gz :Linux:se30os.gz
    hattrib -t BINA -c \"????\" :Linux:se30os.gz
    hls -l :Linux
    humount"
sync
echo "installed $(stat -c %s "$P/build/kernel.gz") bytes as :Linux:se30os.gz"

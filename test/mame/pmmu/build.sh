#!/bin/sh
# build.sh [WORKDIR]: build the se30-mame-pmmu image, MAME 0.289's Mac II
# family driver (which includes the SE/30) with pmmu-limits.patch, on top of
# the se30-mame test image (test/mame/Dockerfile). The MAME source and build
# go in WORKDIR (default ./mame-work, about 3 GB).
set -e
D=$(dirname "$(readlink -f "$0")")
W=$(readlink -f "${1:-mame-work}")
mkdir -p "$W"
cd "$W"
[ -d mame-mame0289 ] || {
    curl -sL https://github.com/mamedev/mame/archive/refs/tags/mame0289.tar.gz | tar xz
    patch -d mame-mame0289 -p1 < "$D/pmmu-limits.patch"
}
sudo docker build -t se30-mame-build -f "$D/Dockerfile.build" "$D"
sudo docker run --rm --user "$(id -u):$(id -g)" -v "$W/mame-mame0289":/src -w /src se30-mame-build \
    make -j"$(nproc)" SUBTARGET=se30 SOURCES=src/mame/apple/macii.cpp USE_QTDEBUG=0 NOWERROR=1 TOOLS=0 REGENIE=1
cp mame-mame0289/se30 "$D/se30"
strip "$D/se30"
sudo docker build -t se30-mame-pmmu -f "$D/Dockerfile.run" "$D"
rm -f "$D/se30"

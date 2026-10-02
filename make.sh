#!/bin/sh
# Run make inside the m68k cross-compiler container, the se30-xbuild image
# made from docker/ (see README).
cd "$(dirname "$0")" && exec sudo docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/src -w /src se30-xbuild make "$@"

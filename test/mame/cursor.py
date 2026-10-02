#!/usr/bin/env python3
# Print bounding boxes of regions that differ between two screenshots.
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert('L'); b = Image.open(sys.argv[2]).convert('L')
w, h = a.size; pa, pb = a.load(), b.load()
pts = [(x, y) for y in range(h) for x in range(w) if pa[x, y] != pb[x, y]]
# cluster by proximity
boxes = []
for x, y in pts:
    for bx in boxes:
        if bx[0] - 12 <= x <= bx[2] + 12 and bx[1] - 12 <= y <= bx[3] + 12:
            bx[0], bx[1], bx[2], bx[3] = min(bx[0], x), min(bx[1], y), max(bx[2], x), max(bx[3], y)
            break
    else:
        boxes.append([x, y, x, y])
for bx in boxes:
    print(f"changed box x={bx[0]}..{bx[2]} y={bx[1]}..{bx[3]}")

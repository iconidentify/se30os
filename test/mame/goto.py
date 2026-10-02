#!/usr/bin/env python3
# goto.py X Y: move the emulated Mac's cursor hotspot to (X, Y) using
# screenshot feedback. Prints the final position.
import subprocess, sys, os
from PIL import Image
W = os.path.dirname(os.path.abspath(__file__))
DATA = [0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
        0x7F80, 0x7C00, 0x6C00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000]
MASK = [0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00, 0xFF80,
        0xFFC0, 0xFFE0, 0xFE00, 0xEF00, 0xCF00, 0x8780, 0x0780, 0x0380]
TPL = [(c, r, 0 if DATA[r] & (0x8000 >> c) else 255)
       for r in range(16) for c in range(16) if MASK[r] & (0x8000 >> c)]

def mc(*cmds):
    subprocess.run([os.path.join(W, "mc"), *cmds], check=True, stdout=subprocess.DEVNULL)

def find_cursor(png):
    im = Image.open(png).convert('L'); px = im.load(); w, h = im.size
    for y in range(-1, h):
        for x in range(-1, w):
            ok = True
            for c, r, v in TPL:
                X, Y = x + c, y + r
                if 0 <= X < w and 0 <= Y < h:
                    if (px[X, Y] > 127) != (v > 127): ok = False; break
                elif c < 2 and r < 2: ok = False; break
            if ok:
                return x + 1, y + 1      # hotspot
    return None

def snap():
    mc("snap cur")
    subprocess.run(["sudo", "chmod", "a+r", os.path.join(W, "shots", "cur.png")])
    return find_cursor(os.path.join(W, "shots", "cur.png"))

tx, ty = int(sys.argv[1]), int(sys.argv[2])
mc("mouse -2400 -2400 24", f"mouse {tx * 3} {ty * 3}")
for _ in range(6):
    pos = snap()
    if pos is None:
        print("cursor not found"); sys.exit(1)
    dx, dy = tx - pos[0], ty - pos[1]
    if abs(dx) <= 1 and abs(dy) <= 1:
        break
    mc(f"mouse {round(dx * 2.6)} {round(dy * 2.6)} 2")
print(f"cursor at {pos}")

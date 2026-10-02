#!/usr/bin/env python3
"""mkdeclrom.py - build a NuBus declaration ROM for testing with MAME.

MAME's Farallon EtherMac 30i (pds30_emac) needs its 16 KB declaration ROM.
Rather than the real one, this writes our own in the documented format
(Designing Cards and Drivers for the Macintosh Family): a format block, a
board sResource and an Ethernet sResource giving the card RAM offset
(MinorBaseOS), its length and a MAC address. The ROM answers on byte
lane 3 only, as many real cards' ROMs do, so the driver's byte-lane
handling gets exercised.

Usage: mkdeclrom.py out.bin [mac]
"""
import struct
import sys

SIZE = 0x4000
LANES = 0x78          # lane 3 only: low nibble 8, high nibble its complement


def main():
    out = sys.argv[1]
    mac = bytes(int(x, 16) for x in (sys.argv[2] if len(sys.argv) > 2
                                     else "02:53:30:00:00:01").split(":"))
    rom = bytearray(SIZE)

    def put(off, data):
        rom[off:off + len(data)] = data

    def entry(at, ident, target=None, value=None):
        """A directory entry: id and a 24-bit offset (from the entry) or value."""
        v = (target - at) & 0xFFFFFF if target is not None else value
        put(at, struct.pack(">I", ident << 24 | v))

    def end(at):
        put(at, struct.pack(">I", 0xFF000000))

    def cstr(s):
        return s.encode() + b"\0"

    root, board, ether = 0x100, 0x200, 0x300
    btype, bname, etype, ename = 0x400, 0x410, 0x440, 0x450
    base, length, macaddr = 0x480, 0x490, 0x4a0

    # root directory: board sResource, then the Ethernet function
    entry(root, 0x01, board)
    entry(root + 4, 0x80, ether)
    end(root + 8)

    entry(board, 0x01, btype)                 # sRsrcType
    entry(board + 4, 0x02, bname)             # sRsrcName
    entry(board + 8, 0x20, value=0x1234)      # BoardId
    end(board + 12)
    put(btype, struct.pack(">HHHH", 0x0001, 0x0000, 0x0000, 0x0000))
    put(bname, cstr("se30os test Ethernet"))

    entry(ether, 0x01, etype)
    entry(ether + 4, 0x02, ename)
    entry(ether + 8, 0x0A, base)              # MinorBaseOS
    entry(ether + 12, 0x0B, length)           # MinorLength
    entry(ether + 16, 0x80, macaddr)          # MAC address
    end(ether + 20)
    put(etype, struct.pack(">HHHH", 0x0004, 0x0001, 0x010C, 0x0001))
    put(ename, cstr("Network_Ethernet_se30os"))
    put(base, struct.pack(">I", 0xD0000))
    put(length, struct.pack(">I", 0x10000))
    put(macaddr, mac)

    # format block: the last 20 bytes
    fb = SIZE - 20
    put(fb, struct.pack(">IIIBBIBB", (root - fb) & 0xFFFFFF, SIZE, 0, 1, 1,
                        0x5A932BC7, 0, LANES))
    open(out, "wb").write(rom)


if __name__ == "__main__":
    main()

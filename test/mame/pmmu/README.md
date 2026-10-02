# MAME with 68030 MMU limit checks

MAME's 68030 MMU stores the limit fields of the root pointers (CRP and SRP) and of long-format table descriptors, but never checks them. A real 68030 does: an index above an upper limit, or below a lower one, sets MMUSR's L and I bits and bus-errors the access.

- **The bug it hid.** se30os once set its root pointer to `0x00000002`, meaning upper limit 0. It booted in MAME but hung a real SE/30 at "mmu...". `pmmu-limits.patch` adds the check, so MAME behaves like the hardware.
- **Building.** `./build.sh [WORKDIR]` downloads MAME 0.289, applies the patch, and builds only the Mac II family driver (`macii.cpp`, which includes the SE/30). It makes the `se30-mame-pmmu` image on top of `se30-mame` (`../Dockerfile`).
- **Using it.** `test/boot.sh` uses the image when it exists.
- **Checked on it.** Mac OS 7.1, Penguin, se30boot, se30os and Linux all run normally.
- **Licence.** The patch is to a BSD-3-Clause file of MAME's and carries the same licence.

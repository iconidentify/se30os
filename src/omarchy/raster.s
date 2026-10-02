| Cached 6x13 glyph stamping for RAM or the SE/30 framebuffer.
| This uses the 68030's legal unaligned RAM word accesses for crossing glyphs.
| Each dirty glyph clears exactly six pixels, then stamps its white bits.
| The single-byte path prevents writing beyond the last byte of a screen row.
        .text
        .even
        .globl raster_diff
raster_diff:
        link %a6,#-4
        movem.l %d2-%d7/%a2-%a5,%sp@-
        clr.l %a6@(-4)
        move.l %a6@(8),%a2
        move.l %a6@(12),%a3
        move.l %a6@(16),%a4
        move.l %a6@(20),%a5
        move.l %a6@(24),%d7
        tst.l %d7
        ble Rreturn
        subq.w #1,%d7
        move.l %a6@(32),%d0
        lsl.l #6,%d0
        adda.l %d0,%a4
Rrow:
        move.l %a6@(28),%d3
        moveq #80,%d5
Rcell:
        moveq #0,%d0
        move.b %a2@(6),%d0
        cmp.b %a3@,%d0
        beq Rnext
        move.b %d0,%a3@
        addq.l #1,%a6@(-4)
        lsl.w #4,%d0
        lea %a5@(0,%d0:w),%a0
        move.l %d3,%d0
        lsr.w #3,%d0
        lea %a4@(0,%d0:w),%a1
        move.l %d3,%d4
        and.w #7,%d4
        move.w #0xfc00,%d6
        lsr.w %d4,%d6
        moveq #12,%d2
        cmp.w #2,%d4
        ble Rbyte
Rword:
        moveq #0,%d0
        move.b %a0@+,%d0
        lsl.w #8,%d0
        lsr.w %d4,%d0
        not.w %d0
        move.w %a1@,%d1
        or.w %d6,%d1
        and.w %d0,%d1
        move.w %d1,%a1@
        lea %a1@(64),%a1
        dbra %d2,Rword
        bra Rnext
Rbyte:
        lsr.w #8,%d6
Rbyte_loop:
        moveq #0,%d0
        move.b %a0@+,%d0
        lsr.b %d4,%d0
        not.b %d0
        move.b %a1@,%d1
        or.b %d6,%d1
        and.b %d0,%d1
        move.b %d1,%a1@
        lea %a1@(64),%a1
        dbra %d2,Rbyte_loop
Rnext:
        addq.l #8,%a2
        addq.l #1,%a3
        addq.w #6,%d3
        dbra %d5,Rcell
        lea %a4@(832),%a4
        dbra %d7,Rrow
Rreturn:
        move.l %a6@(-4),%d0
        movem.l %sp@+,%d2-%d7/%a2-%a5
        unlk %a6
        rts

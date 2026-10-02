| Omarchy/SE30: monochrome adaptation of TTE/ttfx's decrypt effect.
| Hand-written 68000-compatible integer instructions, tuned for the 68030.
| GNU as MIT syntax; A/UX COFF symbols have no leading underscore.
| Each cell: phase.w, ticks.w, units.w, symbol.b, padding.b.
| phase: 0 hidden, 1 typing, 2 fast cipher, 3 slow cipher, 4 discovered.
| The UI is in saver.c; all animation transitions and randomness are here.
| A prebuilt non-space pointer list eliminates empty canvas cells from ticks.
| The fixed list supports up to 972 input cells (the complete 81x12 banner).

        .text
        .even
        .globl decrypt_reset
decrypt_reset:
        link %a6,#0
        movem.l %d2/%a2-%a4,%sp@-
        move.l %a6@(8),%a2
        move.l %a6@(12),%a3
        move.l %a6@(16),%d2
        move.l %a6@(20),%d0
        bne Lseed_ok
        move.l #0x68030,%d0
Lseed_ok:
        move.l %d0,cipher_seed
        clr.l typing_cursor
        clr.w engine_phase
        clr.w typing_wait
        clr.l active_count
        tst.l %d2
        ble Lreset_done
        cmp.l #972,%d2
        bgt Lreset_done
        lea active_entries,%a4
Lreset_cell:
        clr.l %a2@
        clr.l %a2@(4)
        move.b #32,%a2@(6)
        cmp.b #32,%a3@+
        bne Lreset_active
        move.w #4,%a2@
        bra Lreset_next
Lreset_active:
        move.l %a2,%a4@+
        lea %a3@(-1),%a0
        move.l %a0,%a4@+
        addq.l #1,active_count
Lreset_next:
        addq.l #8,%a2
        subq.l #1,%d2
        bne Lreset_cell
Lreset_done:
        movem.l %sp@+,%d2/%a2-%a4
        unlk %a6
        rts

        .globl decrypt_tick
decrypt_tick:
        link %a6,#0
        movem.l %d2-%d7/%a2-%a4,%sp@-
        move.l %a6@(8),%a2
        move.l %a6@(12),%a3
        move.l %a6@(16),%d7
        tst.l %d7
        ble Lcomplete
        cmp.l #972,%d7
        bgt Lcomplete
        tst.w engine_phase
        bne Lanimate

| Type four non-space cells on three of four ticks. Spaces consume no quota.
        move.l typing_cursor,%d2
        cmp.l %d7,%d2
        bge Ltyping_drain
        bsr Lrandom
        and.l #3,%d0
        beq Lanimate
        moveq #4,%d3
        move.l %d2,%d0
        lsl.l #3,%d0
        lea %a2@(0,%d0:l),%a4
Ltype:
        cmp.b #32,%a3@(0,%d2:l)
        beq Ltype_next
        move.w #1,%a4@
        move.w #8,%a4@(2)
        move.b #35,%a4@(6)
        subq.l #1,%d3
Ltype_next:
        addq.l #1,%d2
        addq.l #8,%a4
        cmp.l %d7,%d2
        bge Ltype_store
        tst.l %d3
        bne Ltype
Ltype_store:
        move.l %d2,typing_cursor
        bra Lanimate
Ltyping_drain:
        addq.w #1,typing_wait
        cmp.w #8,typing_wait
        blt Lanimate
        move.w #1,engine_phase
        move.l %d7,%d2
        move.l %a2,%a4
Lbegin_fast:
        cmp.w #4,%a4@
        beq Lbegin_next
        move.w #2,%a4@
        move.w #48,%a4@(2)
        bsr Lsymbol
        move.b %d0,%a4@(6)
Lbegin_next:
        addq.l #8,%a4
        subq.l #1,%d2
        bne Lbegin_fast

Lanimate:
        moveq #0,%d6
        move.l active_count,%d5
        beq Lcomplete
        lea active_entries,%a4
Lcell:
        move.l %a4@+,%a2
        move.l %a4@+,%a3
        move.w %a2@,%d2
        cmp.w #4,%d2
        beq Lcell_next
        addq.l #1,%d6
        tst.w %d2
        beq Lcell_next
        cmp.w #1,%d2
        beq Ltyping
        cmp.w #2,%d2
        beq Lfast

| Slow cipher: retain a symbol for 3..5 or 35..59 ticks, then change it.
        subq.w #1,%a2@(2)
        bne Lcell_next
        subq.w #1,%a2@(4)
        beq Ldiscover
        bsr Lslow_frame
        bra Lcell_next
Ldiscover:
        move.w #4,%a2@
        move.b %a3@,%a2@(6)
        subq.l #1,%d6
        bra Lcell_next
Ltyping:
        tst.w %a2@(2)
        beq Lcell_next
        subq.w #1,%a2@(2)
        beq Ltyped
        moveq #0,%d0
        move.w %a2@(2),%d0
        lsr.w #1,%d0
        lea typing_symbols,%a0
        move.b %a0@(0,%d0:w),%a2@(6)
        bra Lcell_next
Ltyped:
        bsr Lsymbol
        move.b %d0,%a2@(6)
        bra Lcell_next
Lfast:
        subq.w #1,%a2@(2)
        beq Lbegin_slow
        btst #0,%a2@(3)
        bne Lcell_next
        bsr Lsymbol
        move.b %d0,%a2@(6)
        bra Lcell_next
Lbegin_slow:
        move.w #3,%a2@
        bsr Lrandom
        and.l #0xffff,%d0
        divu #6,%d0
        swap %d0
        addq.w #1,%d0
        move.w %d0,%a2@(4)
        bsr Lslow_frame
Lcell_next:
        subq.l #1,%d5
        bne Lcell
        tst.l %d6
        beq Lcomplete
        moveq #0,%d0
        bra Ltick_return
Lcomplete:
        moveq #1,%d0
Ltick_return:
        movem.l %sp@+,%d2-%d7/%a2-%a4
        unlk %a6
        rts

| Internal helpers clobber only d0/d1/a0 (SysV caller-saved registers).
Lslow_frame:
        bsr Lsymbol
        move.b %d0,%a2@(6)
        bsr Lrandom
        and.l #3,%d0
        beq Llong_frame
        bsr Lrandom
        and.l #0xffff,%d0
        divu #3,%d0
        swap %d0
        addq.w #3,%d0
        move.w %d0,%a2@(2)
        rts
Llong_frame:
        bsr Lrandom
        and.l #0xffff,%d0
        divu #25,%d0
        swap %d0
        add.w #35,%d0
        move.w %d0,%a2@(2)
        rts
Lsymbol:
        bsr Lrandom
        and.l #63,%d0
        lea cipher_symbols,%a0
        move.b %a0@(0,%d0:w),%d0
        rts
Lrandom:
        move.l cipher_seed,%d0
        move.l %d0,%d1
        lsl.l #8,%d1
        lsl.l #5,%d1
        eor.l %d1,%d0
        move.l %d0,%d1
        lsr.l #8,%d1
        lsr.l #8,%d1
        lsr.l #1,%d1
        eor.l %d1,%d0
        move.l %d0,%d1
        lsl.l #5,%d1
        eor.l %d1,%d0
        move.l %d0,cipher_seed
        rts

        .data
        .even
cipher_seed:    .long 1
typing_cursor:  .long 0
active_count:   .long 0
engine_phase:   .word 0
typing_wait:    .word 0
typing_symbols: .ascii ".:*#"
cipher_symbols: .ascii "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@%"
        .bss
        .even
active_entries: .space 7776

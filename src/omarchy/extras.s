| Four TTE/ttfx adaptations for 512x342 monochrome: matrix, spotlights,
| laseretch, vhstape. All per-frame animation is hand-written integer 68k.
| Fixed 81x26 canvas, 8-byte cells, ASCII symbol at +6. No allocation.
| Active entries contain cell ptr, source ptr, column.w, row.w (12 bytes).
| Only non-space entries are scanned for illumination and laser targets.
        .text
        .even
        .globl extra_reset
extra_reset:
        link %a6,#0
        movem.l %d2-%d7/%a2-%a5,%sp@-
        cmp.l #2106,%a6@(16)
        bne Ereset_return
        move.l %a6@(24),%d0
        cmp.l #2,%d0
        blt Ereset_return
        cmp.l #5,%d0
        bgt Ereset_return
        move.w %d0,extra_kind
        move.l %a6@(20),%d0
        bne Eseed_ok
        move.l #0x68030,%d0
Eseed_ok:
        move.l %d0,extra_seed
        clr.w extra_frame
        clr.w extra_count
        clr.w laser_cursor
        clr.w laser_overlays
        move.w #-1,vhs_wave
        move.w #-1,vhs_random
        move.l %a6@(8),%a2
        move.l %a6@(12),%a3
        lea extra_active,%a4
        moveq #0,%d3
        moveq #0,%d4
        move.w #2105,%d7
Ereset_cell:
        clr.l %a2@
        clr.l %a2@(4)
        move.b #32,%a2@(6)
        cmp.w #5,extra_kind
        bne Ereset_hidden
        move.b %a3@,%a2@(6)
Ereset_hidden:
        cmp.b #32,%a3@
        beq Ereset_next
        move.l %a2,%a4@+
        move.l %a3,%a4@+
        move.w %d3,%a4@+
        move.w %d4,%a4@+
        addq.w #1,extra_count
Ereset_next:
        addq.l #8,%a2
        addq.l #1,%a3
        addq.w #1,%d3
        cmp.w #81,%d3
        blt Ereset_row
        moveq #0,%d3
        addq.w #1,%d4
Ereset_row:
        dbra %d7,Ereset_cell

| 27 disjoint rain streams. Each is head, countdown, period, length, column.
        lea matrix_streams,%a4
        lea matrix_resolved,%a5
        moveq #0,%d3
        moveq #26,%d7
Ereset_stream:
        bsr Erandom
        move.l %d0,%d1
        and.w #31,%d0
        neg.w %d0
        subq.w #1,%d0
        move.w %d0,%a4@+
        move.w #1,%a4@+
        lsr.l #8,%d1
        move.l %d1,%d0
        and.w #3,%d0
        addq.w #2,%d0
        move.w %d0,%a4@+
        lsr.w #3,%d1
        and.w #7,%d1
        addq.w #4,%d1
        move.w %d1,%a4@+
        move.w %d3,%a4@+
        addq.w #3,%d3
        dbra %d7,Ereset_stream
        moveq #80,%d7
Ereset_resolved:
        clr.b %a5@+
        dbra %d7,Ereset_resolved

| Three moving lights, each column,row,dx,dy. Bounds contain the banner.
        lea light_initial,%a0
        lea light_positions,%a1
        moveq #5,%d7
Ereset_lights:
        move.l %a0@+,%a1@+
        dbra %d7,Ereset_lights
Ereset_return:
        movem.l %sp@+,%d2-%d7/%a2-%a5
        unlk %a6
        rts

        .globl extra_tick
extra_tick:
        link %a6,#0
        movem.l %d2-%d7/%a2-%a5,%sp@-
        cmp.l #2106,%a6@(16)
        bne Ecomplete
        move.l %a6@(8),%a2
        move.l %a6@(12),%a3
        cmp.w #2,extra_kind
        blt Ecomplete
        cmp.w #5,extra_kind
        bgt Ecomplete
        cmp.w #400,extra_frame
        bge Ecomplete
        addq.w #1,extra_frame
        cmp.w #2,extra_kind
        beq Mmatrix
        cmp.w #3,extra_kind
        beq Plights
        cmp.w #4,extra_kind
        beq Llaser
        bra Vvhs
Efinish:
| Exact source restoration, then a stable completion on subsequent ticks.
        move.l %a2,%a0
        move.l %a3,%a1
        move.w #2105,%d7
Efinish_cell:
        move.b %a1@+,%a0@(6)
        addq.l #8,%a0
        dbra %d7,Efinish_cell
        move.w #400,extra_frame
Ecomplete:
        moveq #1,%d0
        bra Ereturn
Enot_done:
        moveq #0,%d0
Ereturn:
        movem.l %sp@+,%d2-%d7/%a2-%a5
        unlk %a6
        rts

Mmatrix:
        cmp.w #322,extra_frame
        bge Efinish
        lea matrix_streams,%a4
        lea matrix_resolved,%a5
        moveq #26,%d7
Mstream:
        moveq #0,%d3
        move.w %a4@(8),%d3
        tst.b %a5@(0,%d3:w)
        bne Mnext
        subq.w #1,%a4@(2)
        bgt Mnext
        move.w %a4@(4),%a4@(2)
        addq.w #1,%a4@
        move.w %a4@,%d4
        move.w %d4,%d2
        sub.w %a4@(6),%d2
        bmi Mhead
        cmp.w #26,%d2
        bge Mrestart
        bsr Moffset
        move.b #32,%a2@(6,%d2:w)
Mhead:
        tst.w %d4
        bmi Mnext
        cmp.w #26,%d4
        bge Mnext
        move.w %d4,%d2
        bsr Moffset
        move.b #35,%a2@(6,%d2:w)
        move.w %d4,%d2
        subq.w #1,%d2
        bmi Mnext
        bsr Moffset
        bsr Esymbol
        move.b %d0,%a2@(6,%d2:w)
        bra Mnext
Mrestart:
        bsr Erandom
        and.w #15,%d0
        neg.w %d0
        subq.w #1,%d0
        move.w %d0,%a4@
Mnext:
        lea %a4@(10),%a4
        dbra %d7,Mstream
        cmp.w #241,extra_frame
        blt Enot_done
| Freeze and resolve one permuted column per frame, with rain continuing
| only in unresolved columns. 31 and 81 are coprime: all columns visited.
        moveq #0,%d0
        move.w extra_frame,%d0
        sub.w #241,%d0
        mulu #31,%d0
        divu #81,%d0
        swap %d0
        move.w %d0,%d3
        move.b #1,%a5@(0,%d3:w)
        move.w %d3,%d2
        lsl.w #3,%d2
        lea %a2@(0,%d2:w),%a0
        lea %a3@(0,%d3:w),%a1
        moveq #25,%d7
Mresolve:
        move.b %a1@,%a0@(6)
        lea %a1@(81),%a1
        lea %a0@(648),%a0
        dbra %d7,Mresolve
        bra Enot_done
Moffset:
| d2=row, d3=column -> d2=cell byte offset; small 68000 word multiply.
        mulu #81,%d2
        add.w %d3,%d2
        lsl.w #3,%d2
        rts

Plights:
        cmp.w #330,extra_frame
        bge Efinish
        lea light_positions,%a4
        moveq #2,%d7
        btst #0,extra_frame+1
        bne Ppositions_done
        cmp.w #200,extra_frame
        bge Pconverge
Pmove:
        move.w %a4@(4),%d0
        add.w %d0,%a4@
        cmp.w #4,%a4@
        ble Pbounce_x
        cmp.w #76,%a4@
        blt Pmove_y
Pbounce_x:
        neg.w %a4@(4)
Pmove_y:
        move.w extra_frame,%d0
        and.w #7,%d0
        bne Pnext
        move.w %a4@(6),%d0
        add.w %d0,%a4@(2)
        cmp.w #5,%a4@(2)
        ble Pbounce_y
        cmp.w #21,%a4@(2)
        blt Pnext
Pbounce_y:
        neg.w %a4@(6)
Pnext:
        addq.l #8,%a4
        dbra %d7,Pmove
        bra Ppositions_done
Pconverge:
        cmp.w #40,%a4@
        beq Pcenter_y
        blt Pcenter_right
        subq.w #1,%a4@
        bra Pcenter_y
Pcenter_right:
        addq.w #1,%a4@
Pcenter_y:
        cmp.w #13,%a4@(2)
        beq Pcenter_next
        blt Pcenter_down
        subq.w #1,%a4@(2)
        bra Pcenter_next
Pcenter_down:
        addq.w #1,%a4@(2)
Pcenter_next:
        addq.l #8,%a4
        dbra %d7,Pconverge
Ppositions_done:
| Word squared-distance table avoids per-character multiply/divide/RNG.
| Terminal rows are twice as tall as columns: dx^2 + (2*dy)^2.
        moveq #11,%d6
        cmp.w #240,extra_frame
        ble Pradius
        move.w extra_frame,%d6
        sub.w #240,%d6
        lsr.w #1,%d6
        add.w #11,%d6
Pradius:
        move.w %d6,%d5
        mulu %d5,%d5
        moveq #0,%d7
        move.w extra_count,%d7
        beq Enot_done
        subq.w #1,%d7
        lea extra_active,%a5
Pcell:
        move.l %a5@,%a0
        move.b #32,%a0@(6)
        lea light_positions,%a4
        moveq #2,%d4
Ptest:
        move.w %a5@(8),%d0
        sub.w %a4@,%d0
        bpl Pdx
        neg.w %d0
Pdx:
        cmp.w %d6,%d0
        bgt Pmiss
        move.w %a5@(10),%d1
        sub.w %a4@(2),%d1
        bpl Pdy
        neg.w %d1
Pdy:
        add.w %d1,%d1
        cmp.w %d6,%d1
        bgt Pmiss
        add.w %d0,%d0
        add.w %d1,%d1
        lea square_table,%a1
        move.w %a1@(0,%d0:w),%d2
        add.w %a1@(0,%d1:w),%d2
        cmp.w %d5,%d2
        bgt Pmiss
        move.l %a5@(4),%a1
        move.b %a1@,%a0@(6)
        bra Pcell_next
Pmiss:
        addq.l #8,%a4
        dbra %d4,Ptest
Pcell_next:
        lea %a5@(12),%a5
        dbra %d7,Pcell
        bra Enot_done

Llaser:
| Restore only last frame's beam/sparks, using settled-cell flags.
        moveq #0,%d7
        move.w laser_overlays,%d7
        beq Ladvance
        subq.w #1,%d7
        lea laser_offsets,%a4
Lrestore:
        moveq #0,%d2
        move.w %a4@+,%d2
        bsr Lrestore_cell
        dbra %d7,Lrestore
Ladvance:
        clr.w laser_overlays
        moveq #0,%d3
        move.w laser_cursor,%d3
        cmp.w extra_count,%d3
        bge Efinish
        lea extra_active,%a5
        move.w %d3,%d0
        mulu #12,%d0
        adda.l %d0,%a5
        moveq #1,%d7
Letch:
        move.l %a5@,%a0
        move.l %a5@(4),%a1
        move.w #1,%a0@
        move.b %a1@,%a0@(6)
        addq.w #1,%d3
        move.w %a5@(8),%d4
        move.w %a5@(10),%d5
        lea %a5@(12),%a5
        cmp.w extra_count,%d3
        bge Letch_done
        dbra %d7,Letch
Letch_done:
        move.w %d3,laser_cursor
        lea laser_offsets,%a4
        move.w %d5,%d2
        mulu #81,%d2
        add.w %d4,%d2
        move.w %d2,%d6
        lsl.w #3,%d2
        move.w %d2,%a4@+
        move.b #42,%a2@(6,%d2:w)
        addq.w #1,laser_overlays
        move.w %d5,%d7
        beq Lsparks
        subq.w #1,%d7
Lbeam:
        addq.w #1,%d4
        cmp.w #81,%d4
        bge Lsparks
        sub.w #640,%d2
        move.w %d2,%a4@+
        move.b #47,%a2@(6,%d2:w)
        addq.w #1,laser_overlays
        dbra %d7,Lbeam
Lsparks:
        lea laser_deltas,%a5
        moveq #7,%d7
Lspark:
        move.w %d6,%d2
        add.w %a5@+,%d2
        bmi Lspark_next
        cmp.w #2106,%d2
        bge Lspark_next
        lsl.w #3,%d2
        move.w %d2,%a4@+
        bsr Erandom
        and.w #1,%d0
        moveq #46,%d1
        tst.w %d0
        beq Lspark_draw
        moveq #42,%d1
Lspark_draw:
        move.b %d1,%a2@(6,%d2:w)
        addq.w #1,laser_overlays
Lspark_next:
        dbra %d7,Lspark
        bra Enot_done
Lrestore_cell:
        move.b #32,%a2@(6,%d2:w)
        tst.w %a2@(0,%d2:w)
        beq Lrestore_return
        move.w %d2,%d0
        lsr.w #3,%d0
        move.b %a3@(0,%d0:w),%a2@(6,%d2:w)
Lrestore_return:
        rts

Vvhs:
        cmp.w #320,extra_frame
        bge Efinish
        cmp.w #225,extra_frame
        bge Vnoise_phase
| Restore at most four rows, not the entire canvas.
        move.w vhs_wave,%d3
        bmi Vrestore_random
        moveq #2,%d7
Vrestore_wave:
        cmp.w #26,%d3
        bge Vrestore_random
        moveq #0,%d4
        bsr Vrow
        addq.w #1,%d3
        dbra %d7,Vrestore_wave
Vrestore_random:
        move.w vhs_random,%d3
        bmi Vwave_start
        moveq #0,%d4
        bsr Vrow
Vwave_start:
        move.w #-1,vhs_random
        moveq #0,%d0
        move.w extra_frame,%d0
        divu #3,%d0
        and.l #0xffff,%d0
        divu #26,%d0
        swap %d0
        move.w %d0,%d3
        move.w %d3,vhs_wave
        moveq #2,%d7
Vwave:
        cmp.w #26,%d3
        bge Vrandom
        moveq #0,%d4
        move.b vhs_offsets+2,%d4
        sub.w %d7,%d4
        add.w %d4,%d4
        btst #6,extra_frame+1
        beq Vwave_draw
        neg.w %d4
Vwave_draw:
        bsr Vrow
        addq.w #1,%d3
        dbra %d7,Vwave
Vrandom:
        bsr Erandom
        and.w #7,%d0
        bne Enot_done
        bsr Erandom
        and.l #0xffff,%d0
        divu #26,%d0
        swap %d0
        move.w %d0,%d3
        move.w %d3,vhs_random
        bsr Erandom
        and.w #7,%d0
        subq.w #4,%d0
        move.w %d0,%d4
        bsr Vrow
        bra Enot_done
Vnoise_phase:
        cmp.w #240,extra_frame
        bge Vredraw
        moveq #0,%d7
        move.w extra_count,%d7
        beq Enot_done
        subq.w #1,%d7
        lea extra_active,%a5
Vnoise:
        bsr Erandom
        and.w #3,%d0
        lea noise_symbols,%a1
        move.l %a5@,%a0
        move.b %a1@(0,%d0:w),%a0@(6)
        lea %a5@(12),%a5
        dbra %d7,Vnoise
        bra Enot_done
Vredraw:
        cmp.w #240,extra_frame
        bne Vredraw_row
        move.l %a2,%a0
        move.w #2105,%d7
Vclear:
        move.b #32,%a0@(6)
        addq.l #8,%a0
        dbra %d7,Vclear
Vredraw_row:
        moveq #0,%d0
        move.w extra_frame,%d0
        sub.w #240,%d0
        divu #3,%d0
        swap %d0
        tst.w %d0
        bne Enot_done
        swap %d0
        move.w %d0,%d3
        cmp.w #26,%d3
        bge Enot_done
        moveq #0,%d4
        bsr Vrow
        bra Enot_done
Vrow:
| d3=row, d4=signed horizontal shift. Preserve loop/state registers.
        move.w %d3,%d0
        mulu #81,%d0
        lea %a3@(0,%d0:l),%a1
        lsl.l #3,%d0
        lea %a2@(0,%d0:l),%a0
        moveq #0,%d1
        moveq #80,%d2
Vrow_cell:
        move.w %d1,%d0
        sub.w %d4,%d0
        bmi Vrow_blank
        cmp.w #81,%d0
        bge Vrow_blank
        move.b %a1@(0,%d0:w),%a0@(6)
        bra Vrow_next
Vrow_blank:
        move.b #32,%a0@(6)
Vrow_next:
        addq.l #8,%a0
        addq.w #1,%d1
        dbra %d2,Vrow_cell
        rts

Esymbol:
        bsr Erandom
        and.w #31,%d0
        lea matrix_symbols,%a0
        move.b %a0@(0,%d0:w),%d0
        rts
Erandom:
| xorshift32, only caller-saved d0/d1 clobbered.
        move.l extra_seed,%d0
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
        move.l %d0,extra_seed
        rts

        .data
        .even
extra_seed:      .long 1
extra_frame:     .word 400
extra_kind:      .word 0
extra_count:     .word 0
laser_cursor:   .word 0
laser_overlays: .word 0
vhs_wave:        .word -1
vhs_random:      .word -1
light_initial:  .word 8,7,2,1, 40,13,-1,1, 72,19,-2,-1
vhs_offsets:    .byte 2,4,6
noise_symbols:  .ascii ".:*#"
matrix_symbols: .ascii "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"
        .even
laser_deltas:   .word -1,1,-81,-80,-82,81,80,82
square_table:
        .word 0,1,4,9,16,25,36,49,64,81,100,121
        .word 144,169,196,225,256,289,324,361,400,441,484,529
        .word 576,625,676,729,784,841,900,961,1024,1089,1156,1225
        .word 1296,1369,1444,1521,1600,1681,1764,1849,1936,2025,2116,2209
        .word 2304,2401,2500,2601,2704,2809,2916,3025,3136,3249,3364,3481
        .word 3600,3721,3844,3969,4096,4225,4356,4489,4624,4761,4900,5041
        .word 5184,5329,5476,5625,5776,5929,6084,6241,6400,6561,6724,6889
        .word 7056,7225,7396,7569,7744,7921,8100,8281,8464,8649,8836,9025
        .word 9216
        .bss
        .even
extra_active:    .space 25272
matrix_streams:  .space 270
matrix_resolved: .space 82
light_positions:.space 24
laser_offsets:  .space 68

| TTE/ttfx monochrome adaptations: fireworks, bouncyballs, expand, beams.
| Integer-only 68030 animation, 81x26 canvas, 8-byte cells; glyph at +6.
| Target entries: cell byte-offset.w, column.w, row.w, prior-position.w.
| Particle pool: 64 * 20 bytes, age,x,y,vx,vy,target,prior,home-x,home-y,pad.
| x/y use signed 8.8 fixed point. Target and prior are cell byte offsets.
        .text
        .even
        .globl motion_reset
motion_reset:
        link %a6,#0
        movem.l %d2-%d7/%a2-%a5,%sp@-
        cmp.l #2106,%a6@(16)
        bne Treset_return
        move.l %a6@(24),%d0
        cmp.l #6,%d0
        blt Treset_return
        cmp.l #9,%d0
        bgt Treset_return
        move.w %d0,motion_kind
        move.l %a6@(20),%d0
        bne Tseed_ok
        move.l #0x68030,%d0
Tseed_ok:
        move.l %d0,motion_seed
        clr.w motion_frame
        clr.w motion_count
        clr.w motion_cursor
        clr.w beam_overlays
        move.w #12,launch_delay
        move.l %a6@(8),%a2
        move.l %a6@(12),%a3
        lea motion_targets,%a4
        moveq #0,%d3
        moveq #0,%d4
        moveq #0,%d5
        move.w #2105,%d7
Treset_cell:
        clr.l %a2@
        clr.l %a2@(4)
        move.b #32,%a2@(6)
        cmp.b #32,%a3@+
        beq Treset_next
        move.w %d5,%a4@+
        move.w %d3,%a4@+
        move.w %d4,%a4@+
        move.w #-1,%a4@+
        addq.w #1,motion_count
Treset_next:
        addq.l #8,%a2
        addq.w #8,%d5
        addq.w #1,%d3
        cmp.w #81,%d3
        blt Treset_row
        moveq #0,%d3
        addq.w #1,%d4
Treset_row:
        dbra %d7,Treset_cell
        lea motion_particles,%a4
        moveq #63,%d7
Treset_particle:
        move.w #-1,%a4@
        move.w #-1,%a4@(12)
        lea %a4@(20),%a4
        dbra %d7,Treset_particle
        lea beam_streams,%a4
        moveq #0,%d3
        moveq #5,%d7
Treset_beam:
        move.w %d3,%d0
        and.w #1,%d0
        move.w %d0,%a4@
        bsr Tbeam_lane
        move.w %d0,%a4@(2)
        move.w #-1,%a4@(4)
        clr.w %a4@(6)
        addq.w #1,%d3
        addq.l #8,%a4
        dbra %d7,Treset_beam
Treset_return:
        movem.l %sp@+,%d2-%d7/%a2-%a5
        unlk %a6
        rts

        .globl motion_tick
motion_tick:
        link %a6,#0
        movem.l %d2-%d7/%a2-%a5,%sp@-
        cmp.l #2106,%a6@(16)
        bne Tcomplete
        move.l %a6@(8),%a2
        move.l %a6@(12),%a3
        cmp.w #6,motion_kind
        blt Tcomplete
        cmp.w #9,motion_kind
        bgt Tcomplete
        cmp.w #4096,motion_frame
        bge Tcomplete
        addq.w #1,motion_frame
        cmp.w #8,motion_kind
        beq Xexpand
        cmp.w #9,motion_kind
        beq Hbeams
        bra Tparticles
Tfinish:
        move.l %a2,%a0
        move.l %a3,%a1
        move.w #2105,%d7
Tfinish_cell:
        move.b %a1@+,%a0@(6)
        addq.l #8,%a0
        dbra %d7,Tfinish_cell
        move.w #4096,motion_frame
Tcomplete:
        moveq #1,%d0
        bra Treturn
Tnot_done:
        moveq #0,%d0
Treturn:
        movem.l %sp@+,%d2-%d7/%a2-%a5
        unlk %a6
        rts

Tparticles:
| Restore ALL previous overlays before updating ANY particles. Settled cells
| are retained beneath trails, including when two particles share a cell.
        lea motion_particles,%a4
        moveq #63,%d7
Trestore_particle:
        moveq #0,%d2
        move.w %a4@(12),%d2
        bmi Trestore_next
        bsr Trestore_cell
        move.w #-1,%a4@(12)
Trestore_next:
        lea %a4@(20),%a4
        dbra %d7,Trestore_particle
        lea motion_particles,%a4
        moveq #63,%d7
        moveq #0,%d6
Tadvance_particle:
        tst.w %a4@
        bmi Tadvance_next
        addq.w #1,%a4@
        cmp.w #7,motion_kind
        beq Badvance
Fadvance:
        cmp.w #48,%a4@
        bge Tsettle
        cmp.w #12,%a4@
        beq Fburst
        cmp.w #24,%a4@
        beq Fhome
        cmp.w #12,%a4@
        blt Fmove_y
        cmp.w #24,%a4@
        bge Fmove_xy
        addq.w #8,%a4@(8)
Fmove_xy:
        move.w %a4@(6),%d0
        add.w %d0,%a4@(2)
Fmove_y:
        move.w %a4@(8),%d0
        add.w %d0,%a4@(4)
        bra Tlive
Fburst:
| Slot number modulo 16 selects a radial launch velocity from a small table.
        moveq #63,%d0
        sub.w %d7,%d0
        and.w #15,%d0
        lsl.w #2,%d0
        lea burst_vectors,%a0
        move.w %a0@(0,%d0:w),%a4@(6)
        move.w %a0@(2,%d0:w),%a4@(8)
        bra Fmove_xy
Fhome:
| Compute constant return velocities once, not a division every tick.
        move.w %a4@(14),%d0
        lsl.w #8,%d0
        sub.w %a4@(2),%d0
        ext.l %d0
        divs #24,%d0
        move.w %d0,%a4@(6)
        move.w %a4@(16),%d0
        lsl.w #8,%d0
        sub.w %a4@(4),%d0
        ext.l %d0
        divs #24,%d0
        move.w %d0,%a4@(8)
        bra Fmove_xy
Badvance:
        cmp.w #40,%a4@
        bge Tsettle
| Precomputed out-bounce trajectory. Same-column drop matches upstream.
        move.w %a4@,%d0
        add.w %d0,%d0
        lea bounce_curve,%a0
        move.w %a0@(0,%d0:w),%d0
        mulu %a4@(16),%d0
        move.w %d0,%a4@(4)
        bra Tlive
Tsettle:
        moveq #0,%d2
        move.w %a4@(10),%d2
        move.w #1,%a2@(0,%d2:w)
        bsr Trestore_cell
        move.w #-1,%a4@
        bra Tadvance_next
Tlive:
        addq.w #1,%d6
Tadvance_next:
        lea %a4@(20),%a4
        dbra %d7,Tadvance_particle

| A fireworks shell launches 16 characters every 12 ticks. Balls launch
| three characters every other tick. Exhaustion never overwrites a live slot.
        cmp.w #7,motion_kind
        beq Bquota
        subq.w #1,launch_delay
        bgt Tdraw_particles
        move.w #12,launch_delay
        moveq #16,%d5
        bsr Trandom
        and.l #0xffff,%d0
        divu #53,%d0
        swap %d0
        add.w #14,%d0
        lsl.w #8,%d0
        move.w %d0,shell_x
        bsr Trandom
        and.w #3,%d0
        lsl.w #6,%d0
        add.w #320,%d0
        neg.w %d0
        move.w %d0,shell_vy
        bra Tspawn
Bquota:
        btst #0,motion_frame+1
        bne Tdraw_particles
        moveq #3,%d5
Tspawn:
        lea motion_particles,%a4
        lea motion_targets,%a5
        moveq #0,%d3
        move.w motion_cursor,%d3
        move.w %d3,%d0
        lsl.w #3,%d0
        adda.w %d0,%a5
        moveq #63,%d7
Tspawn_slot:
        cmp.w motion_count,%d3
        bge Tspawn_done
        tst.w %a4@
        bpl Tspawn_next
        clr.w %a4@
        move.w %a5@,%a4@(10)
        move.w %a5@(2),%a4@(14)
        move.w %a5@(4),%a4@(16)
        move.w #-1,%a4@(12)
        clr.w %a4@(6)
        cmp.w #7,motion_kind
        beq Bspawn
        move.w shell_x,%a4@(2)
        move.w #6400,%a4@(4)
        move.w shell_vy,%a4@(8)
        bra Tspawned
Bspawn:
        move.w %a5@(2),%d0
        lsl.w #8,%d0
        move.w %d0,%a4@(2)
        clr.w %a4@(4)
        clr.w %a4@(8)
Tspawned:
        addq.w #1,%d3
        addq.w #1,%d6
        addq.l #8,%a5
        subq.w #1,%d5
        beq Tspawn_done
Tspawn_next:
        lea %a4@(20),%a4
        dbra %d7,Tspawn_slot
Tspawn_done:
        move.w %d3,motion_cursor
Tdraw_particles:
        lea motion_particles,%a4
        moveq #63,%d7
Tdraw_particle:
        tst.w %a4@
        bmi Tdraw_next
        move.w %a4@(2),%d3
        asr.w #8,%d3
        bmi Tdraw_next
        cmp.w #81,%d3
        bge Tdraw_next
        move.w %a4@(4),%d4
        asr.w #8,%d4
        bmi Tdraw_next
        cmp.w #26,%d4
        bge Tdraw_next
        move.w %d4,%d2
        mulu #81,%d2
        add.w %d3,%d2
        lsl.w #3,%d2
        move.w %d2,%a4@(12)
        cmp.w #7,motion_kind
        beq Bsymbol
        moveq #111,%d0
        cmp.w #12,%a4@
        blt Tparticle_symbol
        moveq #42,%d0
        cmp.w #20,%a4@
        blt Tparticle_symbol
        moveq #46,%d0
        cmp.w #24,%a4@
        blt Tparticle_symbol
        move.w %a4@(10),%d0
        lsr.w #3,%d0
        move.b %a3@(0,%d0:w),%d0
        bra Tparticle_symbol
Bsymbol:
        moveq #79,%d0
        cmp.w #20,%a4@
        blt Tparticle_symbol
        moveq #111,%d0
        cmp.w #32,%a4@
        blt Tparticle_symbol
        moveq #46,%d0
Tparticle_symbol:
        move.b %d0,%a2@(6,%d2:w)
Tdraw_next:
        lea %a4@(20),%a4
        dbra %d7,Tdraw_particle
        tst.w %d6
        bne Tnot_done
        move.w motion_cursor,%d0
        cmp.w motion_count,%d0
        blt Tnot_done
        bra Tfinish

Xexpand:
        cmp.w #180,motion_frame
        bge Tfinish
| Table-driven InOutQuart scale, 0..256. Build axis lookup maps once per
| frame, then stamp all non-space targets with no per-target multiplication.
        move.w motion_frame,%d0
        add.w %d0,%d0
        lea expand_curve,%a0
        move.w %a0@(0,%d0:w),%d6
        lea expand_x,%a4
        moveq #-40,%d3
        moveq #80,%d7
Xmap_x:
        move.w %d3,%d0
        muls %d6,%d0
        add.l #128,%d0
        asr.l #8,%d0
        add.w #40,%d0
        lsl.w #3,%d0
        move.w %d0,%a4@+
        addq.w #1,%d3
        dbra %d7,Xmap_x
        lea expand_y,%a4
        moveq #-13,%d3
        moveq #25,%d7
Xmap_y:
        move.w %d3,%d0
        muls %d6,%d0
        add.l #128,%d0
        asr.l #8,%d0
        add.w #13,%d0
        mulu #648,%d0
        move.w %d0,%a4@+
        addq.w #1,%d3
        dbra %d7,Xmap_y
        moveq #0,%d7
        move.w motion_count,%d7
        beq Tnot_done
        subq.w #1,%d7
        lea motion_targets,%a5
Xrestore:
        move.w %a5@(6),%d2
        bmi Xrestore_next
        move.b #32,%a2@(6,%d2:w)
Xrestore_next:
        addq.l #8,%a5
        dbra %d7,Xrestore
        lea motion_targets,%a5
        lea expand_x,%a4
        lea expand_y,%a0
        move.w motion_count,%d7
        subq.w #1,%d7
Xdraw:
        move.w %a5@(2),%d0
        add.w %d0,%d0
        move.w %a4@(0,%d0:w),%d2
        move.w %a5@(4),%d0
        add.w %d0,%d0
        add.w %a0@(0,%d0:w),%d2
        move.w %d2,%a5@(6)
        move.w %a5@,%d0
        lsr.w #3,%d0
        move.b %a3@(0,%d0:w),%a2@(6,%d2:w)
        addq.l #8,%a5
        dbra %d7,Xdraw
        bra Tnot_done

Hbeams:
        cmp.w #320,motion_frame
        bge Tfinish
        lea beam_offsets,%a5
        moveq #0,%d7
        move.w beam_overlays,%d7
        beq Hrestore_done
        subq.w #1,%d7
Hrestore:
        moveq #0,%d2
        move.w %a5@+,%d2
        bsr Trestore_cell
        dbra %d7,Hrestore
Hrestore_done:
        clr.w beam_overlays
        cmp.w #240,motion_frame
        bge Hwipe
        lea beam_streams,%a4
        lea beam_offsets,%a5
        moveq #5,%d7
Hstream:
        tst.w %a4@
        beq Hrow_step
        btst #0,motion_frame+1
        bne Hdraw_column
        addq.w #1,%a4@(4)
        cmp.w #26,%a4@(4)
        blt Hdraw_column
        bsr Tbeam_lane
        move.w %d0,%a4@(2)
        clr.w %a4@(4)
Hdraw_column:
        move.w %a4@(4),%d4
        bmi Hnext
        move.w %a4@(2),%d3
        move.w %d4,%d2
        mulu #81,%d2
        add.w %d3,%d2
        lsl.w #3,%d2
        move.w #1,%a2@(0,%d2:w)
        moveq #124,%d5
        bsr Hoverlay
        tst.w %d4
        beq Hnext
        sub.w #648,%d2
        moveq #58,%d5
        bsr Hoverlay
        bra Hnext
Hrow_step:
        addq.w #2,%a4@(4)
        cmp.w #81,%a4@(4)
        blt Hdraw_row
        bsr Tbeam_lane
        move.w %d0,%a4@(2)
        clr.w %a4@(4)
Hdraw_row:
        move.w %a4@(4),%d3
        move.w %a4@(2),%d4
        move.w %d4,%d2
        mulu #81,%d2
        add.w %d3,%d2
        lsl.w #3,%d2
        move.w #1,%a2@(0,%d2:w)
        moveq #61,%d5
        bsr Hoverlay
        tst.w %d3
        beq Hnext
        subq.w #8,%d2
        move.w #1,%a2@(0,%d2:w)
        moveq #42,%d5
        bsr Hoverlay
        cmp.w #2,%d3
        blt Hnext
        subq.w #8,%d2
        moveq #58,%d5
        bsr Hoverlay
Hnext:
        addq.l #8,%a4
        dbra %d7,Hstream
        bra Tnot_done
Hoverlay:
        move.w %d2,%a5@+
        addq.w #1,beam_overlays
        move.b %d5,%a2@(6,%d2:w)
        rts
Hwipe:
| Final diagonal sweep settles every cell, two diagonals per tick.
        move.w motion_frame,%d6
        sub.w #240,%d6
        add.w %d6,%d6
        lea motion_targets,%a5
        moveq #0,%d7
        move.w motion_count,%d7
        beq Tnot_done
        subq.w #1,%d7
Hwipe_target:
        move.w %a5@(2),%d0
        add.w %a5@(4),%d0
        cmp.w %d6,%d0
        bgt Hwipe_next
        move.w %a5@,%d2
        move.w #1,%a2@(0,%d2:w)
        bsr Trestore_cell
Hwipe_next:
        addq.l #8,%a5
        dbra %d7,Hwipe_target
        bra Tnot_done
Tbeam_lane:
        bsr Trandom
        and.l #0xffff,%d0
        tst.w %a4@
        beq Tbeam_row_lane
        divu #81,%d0
        bra Tbeam_remainder
Tbeam_row_lane:
        divu #26,%d0
Tbeam_remainder:
        swap %d0
        rts
Trestore_cell:
        move.b #32,%a2@(6,%d2:w)
        tst.w %a2@(0,%d2:w)
        beq Trestore_return
        move.w %d2,%d0
        lsr.w #3,%d0
        move.b %a3@(0,%d0:w),%a2@(6,%d2:w)
Trestore_return:
        rts
Trandom:
        move.l motion_seed,%d0
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
        move.l %d0,motion_seed
        rts
        .data
        .even
motion_seed:    .long 1
motion_frame:   .word 4096
motion_kind:    .word 0
motion_count:   .word 0
motion_cursor:  .word 0
beam_overlays:  .word 0
launch_delay:   .word 12
shell_x:        .word 0
shell_vy:       .word 0
burst_vectors:
        .word 256,0, 237,49, 181,91, 98,118
        .word 0,128, -98,118, -181,91, -237,49
        .word -256,0, -237,-49, -181,-91, -98,-118
        .word 0,-128, 98,-118, 181,-91, 237,-49
bounce_curve:
        .word 0,1,5,11,19,30,44,59,77,98
        .word 121,146,174,204,237,248,233,220,210,202
        .word 196,193,192,194,198,204,213,224,238,254
        .word 249,244,241,240,242,246,253,254,252,253
        .word 256
expand_curve:
        .word 0,0,0,0,0,0,0,0,0,0,0,0
        .word 0,0,0,0,0,0,0,0,0,0,0,1
        .word 1,1,1,1,1,1,2,2,2,2,3,3
        .word 3,4,4,5,5,6,6,7,7,8,9,10
        .word 10,11,12,13,14,15,17,18,19,21,22,24
        .word 25,27,29,31,33,35,37,39,42,44,47,50
        .word 52,55,59,62,65,69,72,76,80,84,88,93
        .word 97,102,107,112,117,122,128,134,139,144,149,154
        .word 159,163,168,172,176,180,184,187,191,194,197,201
        .word 204,206,209,212,214,217,219,221,223,225,227,229
        .word 231,232,234,235,237,238,239,241,242,243,244,245
        .word 246,246,247,248,249,249,250,250,251,251,252,252
        .word 253,253,253,254,254,254,254,255,255,255,255,255
        .word 255,255,256,256,256,256,256,256,256,256,256,256
        .word 256,256,256,256,256,256,256,256,256,256,256,256
        .word 256
        .bss
        .even
motion_targets:  .space 16848
motion_particles:.space 1280
beam_streams:    .space 48
beam_offsets:    .space 36
expand_x:        .space 162
expand_y:        .space 52

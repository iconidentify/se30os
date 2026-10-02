| Monochrome TTE/ttfx thunderstorm adaptation. No floating point or allocation.
| 81x26 grid; 32 fixed raindrops, 26 bolt cells, 12 branch cells, 8 sparks.
| Cell symbol is at +6 in the shared 8-byte DecryptCell layout.
| A base glyph grid is retained; rain/bolts overlay it, then the banner returns.
| Each drop holds a WORD byte-offset into the grid; next row/right = +656.
| One drop spawns per tick. The right edge wraps cleanly into the next row.
| Only 26-64 touched cells are restored per tick; no full-grid clearing.

        .text
        .even
        .globl storm_reset
storm_reset:
        link %a6,#0
        movem.l %d2-%d3/%a2-%a3,%sp@-
        move.l %a6@(8),%a2
        move.l %a6@(12),%a3
        cmp.l #2106,%a6@(16)
        bne Sreset_return
        move.l %a6@(20),%d0
        bne Sseed_ok
        move.l #0x68030,%d0
Sseed_ok:
        move.l %d0,storm_seed
        clr.w storm_frame
        move.w #40,strike_delay
        clr.w bolt_age
        clr.w spawn_cursor
        lea rain_offsets,%a0
        moveq #31,%d0
Sreset_rain:
        move.w #-1,%a0@+
        dbra %d0,Sreset_rain
        lea bolt_offsets,%a0
        moveq #45,%d0
Sreset_bolt:
        move.w #-1,%a0@+
        dbra %d0,Sreset_bolt
        lea storm_base,%a0
        move.w #2105,%d2
Sreset_grid:
        clr.l %a2@
        clr.l %a2@(4)
        move.b %a3@+,%a2@(6)
        move.b %a2@(6),%a0@+
        addq.l #8,%a2
        dbra %d2,Sreset_grid
Sreset_return:
        movem.l %sp@+,%d2-%d3/%a2-%a3
        unlk %a6
        rts

        .globl storm_tick
storm_tick:
        link %a6,#0
        movem.l %d2-%d7/%a2-%a5,%sp@-
        move.l %a6@(8),%a2
        move.l %a6@(12),%a3
        cmp.l #2106,%a6@(16)
        bne Sbad_count
        lea storm_base,%a5

| Remove last frame's rain and bolt overlays by restoring their base glyphs.
        lea rain_offsets,%a4
        moveq #31,%d7
Srestore_rain:
        moveq #0,%d2
        move.w %a4@+,%d2
        bmi Srestore_rain_next
        bsr Srestore_cell
Srestore_rain_next:
        dbra %d7,Srestore_rain
        lea bolt_offsets,%a4
        moveq #45,%d7
Srestore_bolt:
        moveq #0,%d2
        move.w %a4@+,%d2
        bmi Srestore_bolt_next
        bsr Srestore_cell
Srestore_bolt_next:
        dbra %d7,Srestore_bolt

        addq.w #1,storm_frame
        cmp.w #240,storm_frame
        bge Sfinish

| Pre-storm fade uses stable spatial dithering, not temporal flashing.
| Four passes reduce the banner to 1/4 density. No per-cell RNG or multiply.
        move.w storm_frame,%d0
        cmp.w #1,%d0
        beq Sfade_first
        cmp.w #8,%d0
        beq Sfade_more
        cmp.w #16,%d0
        beq Sfade_more
        bra Srain
Sfade_first:
        moveq #1,%d3
        bra Sfade
Sfade_more:
        moveq #3,%d3
Sfade:
        lea storm_base,%a0
        move.l %a2,%a1
        moveq #0,%d2
        move.w #2105,%d7
Sfade_cell:
        move.l %d2,%d1
        and.w %d3,%d1
        beq Sfade_keep
        move.b #32,%a0@
Sfade_keep:
        move.b %a0@+,%a1@(6)
        addq.l #8,%a1
        addq.w #1,%d2
        dbra %d7,Sfade_cell

Srain:
| Advance live drops by one row and column (81+1 cells = 656 bytes).
        lea rain_offsets,%a4
        moveq #31,%d7
Srain_move:
        moveq #0,%d2
        move.w %a4@,%d2
        bmi Srain_next
        add.w #656,%d2
        cmp.w #16848,%d2
        bge Srain_dead
        move.w %d2,%a4@
        move.b #92,%a2@(6,%d2:w)
        bra Srain_next
Srain_dead:
        move.w #-1,%a4@
Srain_next:
        addq.l #2,%a4
        dbra %d7,Srain_move

| Reuse one pool slot. Initial columns cover the full screen.
        moveq #0,%d7
Srain_spawn:
        bsr Srandom
        and.l #0xffff,%d0
        divu #81,%d0
        swap %d0
        lsl.w #3,%d0
        move.w spawn_cursor,%d1
        lea rain_offsets,%a4
        move.w %d0,%a4@(0,%d1:w)
        move.b #46,%a2@(6,%d0:w)
        addq.w #2,%d1
        and.w #63,%d1
        move.w %d1,spawn_cursor
        dbra %d7,Srain_spawn

| A strike grows down the screen, holds, then cools to sparse text again.
        tst.w bolt_age
        beq Sstrike_wait
        addq.w #1,bolt_age
        cmp.w #32,bolt_age
        bge Sstrike_cool
        bra Sdraw_bolt
Sstrike_cool:
        clr.w bolt_age
        lea bolt_offsets,%a0
        moveq #45,%d0
Sclear_bolt:
        move.w #-1,%a0@+
        dbra %d0,Sclear_bolt
        move.w #48,strike_delay
        bra Snot_done
Sstrike_wait:
        subq.w #1,strike_delay
        bne Snot_done
        move.w #1,bolt_age
        bsr Srandom
        and.l #0xffff,%d0
        divu #32,%d0
        swap %d0
        add.w #24,%d0
        move.w %d0,%d3
        lea bolt_offsets,%a4
        lea bolt_symbols,%a0
        moveq #0,%d4
        moveq #25,%d7
Sbuild_bolt:
        move.w %d4,%d2
        add.w %d3,%d2
        lsl.w #3,%d2
        move.w %d2,%a4@+
        bsr Srandom
        and.l #3,%d0
        move.b #124,%a0@+
        tst.w %d0
        beq Sbolt_left
        cmp.w #2,%d0
        bne Sbolt_bounds
        move.b #92,%a0@(-1)
        addq.w #1,%d3
        bra Sbolt_bounds
Sbolt_left:
        move.b #47,%a0@(-1)
        subq.w #1,%d3
Sbolt_bounds:
        cmp.w #2,%d3
        bge Sbolt_right
        moveq #2,%d3
Sbolt_right:
        cmp.w #78,%d3
        ble Sbolt_row
        moveq #78,%d3
Sbolt_row:
        add.w #81,%d4
        dbra %d7,Sbuild_bolt
| Branch from row 10, down and left across twelve rows.
        move.w bolt_offsets+20,%d2
        moveq #11,%d7
Sbuild_branch:
        add.w #640,%d2
        move.w %d2,%a4@+
        dbra %d7,Sbuild_branch
        lea spark_deltas,%a0
        moveq #7,%d7
Sbuild_sparks:
        move.w bolt_offsets+50,%d2
        add.w %a0@+,%d2
        move.w %d2,%a4@+
        dbra %d7,Sbuild_sparks

Sdraw_bolt:
        lea bolt_offsets,%a4
        moveq #0,%d6
        move.w bolt_age,%d6
        add.w %d6,%d6
        cmp.w #26,%d6
        ble Sbolt_limit
        moveq #26,%d6
Sbolt_limit:
        move.l %d6,%d7
        subq.w #1,%d7
        lea bolt_symbols,%a0
Sbolt_draw_cell:
        move.w %a4@+,%d2
        move.b %a0@+,%a2@(6,%d2:w)
        dbra %d7,Sbolt_draw_cell
        cmp.w #26,%d6
        bne Snot_done
        lea bolt_offsets+52,%a4
        moveq #11,%d7
Sbranch_draw:
        move.w %a4@+,%d2
        move.b #47,%a2@(6,%d2:w)
        dbra %d7,Sbranch_draw
        cmp.w #20,bolt_age
        bgt Sglow_check
        lea bolt_offsets+76,%a4
        moveq #7,%d7
        moveq #42,%d3
        cmp.w #18,bolt_age
        blt Sdraw_sparks
        moveq #46,%d3
Sdraw_sparks:
        move.w %a4@+,%d2
        move.b %d3,%a2@(6,%d2:w)
        dbra %d7,Sdraw_sparks
| The text lights up at impact, then returns to its dithered storm state.
Sglow_check:
        cmp.w #14,bolt_age
        beq Sglow_on
        cmp.w #22,bolt_age
        beq Sglow_off
        bra Snot_done
Sglow_on:
        lea storm_base,%a0
        move.l %a2,%a1
        move.w #2105,%d7
        moveq #0,%d2
Sglow_cells:
        move.b %a3@(0,%d2:w),%d0
        cmp.b #32,%d0
        beq Sglow_next
        move.b %d0,%a0@(0,%d2:w)
        move.b %d0,%a1@(6)
Sglow_next:
        addq.w #1,%d2
        addq.l #8,%a1
        dbra %d7,Sglow_cells
        bra Snot_done
Sglow_off:
        moveq #3,%d3
        bra Sfade

Snot_done:
        moveq #0,%d0
        bra Sreturn
Sfinish:
        move.l %a2,%a1
        move.w #2105,%d7
Sfinish_cells:
        move.b %a3@+,%a1@(6)
        addq.l #8,%a1
        dbra %d7,Sfinish_cells
Sbad_count:
        moveq #1,%d0
Sreturn:
        movem.l %sp@+,%d2-%d7/%a2-%a5
        unlk %a6
        rts

Srestore_cell:
        move.l %d2,%d0
        lsr.w #3,%d0
        move.b %a5@(0,%d0:w),%a2@(6,%d2:w)
        rts
Srandom:
        move.l storm_seed,%d0
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
        move.l %d0,storm_seed
        rts

        .data
        .even
storm_seed:    .long 1
storm_frame:   .word 0
strike_delay:  .word 0
bolt_age:      .word 0
spawn_cursor:  .word 0
rain_offsets:  .space 64
bolt_offsets:  .space 92
bolt_symbols:  .space 26
spark_deltas:  .word -16,-8,8,16,-648,-640,-656,-1296
        .bss
        .even
storm_base:    .space 2106

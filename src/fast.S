| fast.S - the inner loops that matter: memory copy and fill, the
| blitter, and terminal text.

	.text

| void *memcpy(void *dst, const void *src, unsigned long n)
| Longs (four at a time) once dst is long aligned; the 68030 copes with
| a misaligned source.
	.globl	memcpy
memcpy:
	move.l	4(%sp),%a1
	move.l	8(%sp),%a0
	move.l	12(%sp),%d1
	move.l	%a1,%d0
	cmp.l	#16,%d1
	bcs.s	.Lcbytes
1:	move.l	%a1,%d0			| align the destination
	and.w	#3,%d0
	beq.s	2f
	move.b	(%a0)+,(%a1)+
	subq.l	#1,%d1
	bra.s	1b
2:	move.l	%d1,%d0
	lsr.l	#4,%d0
	beq.s	4f
3:	move.l	(%a0)+,(%a1)+
	move.l	(%a0)+,(%a1)+
	move.l	(%a0)+,(%a1)+
	move.l	(%a0)+,(%a1)+
	subq.l	#1,%d0
	bne.s	3b
4:	and.w	#15,%d1
	beq.s	.Lcdone
.Lcbytes:
	subq.w	#1,%d1
	bmi.s	.Lcdone
5:	move.b	(%a0)+,(%a1)+
	dbra	%d1,5b
.Lcdone:
	move.l	4(%sp),%d0
	rts

| void *memset(void *dst, int c, unsigned long n)
	.globl	memset
memset:
	move.l	%d2,-(%sp)
	move.l	8(%sp),%a0
	moveq	#0,%d0
	move.b	15(%sp),%d0
	mulu.l	#0x01010101,%d0		| the byte in all four lanes
	move.l	16(%sp),%d1
	cmp.l	#16,%d1
	bcs.s	.Lsbytes
1:	move.w	%a0,%d2			| align the destination
	and.w	#3,%d2
	beq.s	2f
	move.b	%d0,(%a0)+
	subq.l	#1,%d1
	bra.s	1b
2:	move.l	%d1,%d2
	lsr.l	#4,%d2
	beq.s	4f
3:	move.l	%d0,(%a0)+
	move.l	%d0,(%a0)+
	move.l	%d0,(%a0)+
	move.l	%d0,(%a0)+
	subq.l	#1,%d2
	bne.s	3b
4:	and.w	#15,%d1
.Lsbytes:
	subq.w	#1,%d1
	bmi.s	.Lsdone
5:	move.b	%d0,(%a0)+
	dbra	%d1,5b
.Lsdone:
	move.l	(%sp)+,%d2
	move.l	4(%sp),%d0
	rts

| void blit_fwd(u32 *d, const u32 *s, int rows, int n, int sh, u32 lm,
|		u32 rm, long dstep, long sstep)
| Copy rows of n+1 longs left to right. Destination long j gets
| (s[j] << sh) | (s[j+1] >> (32 - sh)); the first is masked by lm and,
| when n > 0, the last by rm.
	.globl	blit_fwd
blit_fwd:
	movem.l	%d2-%d7/%a2-%a5,-(%sp)
	move.l	44(%sp),%a0
	move.l	48(%sp),%a1
	move.l	52(%sp),%d7
	move.l	60(%sp),%d6
	move.l	72(%sp),%a2
	move.l	76(%sp),%a3
	subq.l	#1,%d7
	bmi	.Lbdone
	tst.l	%d6
	bne	.Lbshift

| aligned: whole longs move as they are
.Lbrow0:
	move.l	%a0,%a4
	move.l	%a1,%a5
	move.l	(%a5)+,%d0
	move.l	(%a4),%d1
	eor.l	%d1,%d0
	and.l	64(%sp),%d0
	eor.l	%d1,%d0
	move.l	%d0,(%a4)+
	move.l	56(%sp),%d2		| n
	beq.s	.Lbnext0
	subq.l	#1,%d2			| middle longs
	move.l	%d2,%d3
	lsr.l	#2,%d3
	beq.s	2f
1:	move.l	(%a5)+,(%a4)+
	move.l	(%a5)+,(%a4)+
	move.l	(%a5)+,(%a4)+
	move.l	(%a5)+,(%a4)+
	subq.l	#1,%d3
	bne.s	1b
2:	and.w	#3,%d2
	bra.s	4f
3:	move.l	(%a5)+,(%a4)+
4:	dbra	%d2,3b
	move.l	(%a5)+,%d0		| last long
	move.l	(%a4),%d1
	eor.l	%d1,%d0
	and.l	68(%sp),%d0
	eor.l	%d1,%d0
	move.l	%d0,(%a4)
.Lbnext0:
	add.l	%a2,%a0
	add.l	%a3,%a1
	dbra	%d7,.Lbrow0
	bra	.Lbdone

| shifted: d0 holds the source long carried over, d3 = 32 - sh
.Lbshift:
	moveq	#32,%d3
	sub.l	%d6,%d3
.Lbrow1:
	move.l	%a0,%a4
	move.l	%a1,%a5
	move.l	(%a5)+,%d0
	move.l	(%a5)+,%d1
	move.l	%d0,%d2
	lsl.l	%d6,%d2
	move.l	%d1,%d0
	lsr.l	%d3,%d1
	or.l	%d1,%d2
	move.l	(%a4),%d5		| first long, masked
	eor.l	%d5,%d2
	and.l	64(%sp),%d2
	eor.l	%d5,%d2
	move.l	%d2,(%a4)+
	move.l	56(%sp),%d4		| n
	beq.s	.Lbnext1
	subq.l	#2,%d4
	bmi.s	2f
1:	move.l	(%a5)+,%d1		| middle longs
	move.l	%d0,%d2
	lsl.l	%d6,%d2
	move.l	%d1,%d0
	lsr.l	%d3,%d1
	or.l	%d1,%d2
	move.l	%d2,(%a4)+
	dbra	%d4,1b
2:	move.l	(%a5)+,%d1		| last long, masked
	move.l	%d0,%d2
	lsl.l	%d6,%d2
	lsr.l	%d3,%d1
	or.l	%d1,%d2
	move.l	(%a4),%d5
	eor.l	%d5,%d2
	and.l	68(%sp),%d2
	eor.l	%d5,%d2
	move.l	%d2,(%a4)
.Lbnext1:
	add.l	%a2,%a0
	add.l	%a3,%a1
	dbra	%d7,.Lbrow1
.Lbdone:
	movem.l	(%sp)+,%d2-%d7/%a2-%a5
	rts

| void text_rows(u32 *dst, const u16 *glyphs, int groups, const u8 *ftab,
|		 long stride, long rowbytes, int height)
| Draw groups of 16 characters, 6 pixels each, starting on a long
| boundary: 96 pixels make exactly three longs per pixel row. ftab holds
| height tables of stride bytes, one per pixel row, giving each glyph's
| six pixels in that row.
	.globl	text_rows
text_rows:
	movem.l	%d2-%d7/%a2-%a5,-(%sp)
	move.l	44(%sp),%a4
	move.l	48(%sp),%a3
	move.l	52(%sp),%d7
	move.l	56(%sp),%a2
	move.l	60(%sp),%d4
	move.l	64(%sp),%a5
	move.l	68(%sp),%d6
	subq.l	#1,%d6
	subq.l	#1,%d7
	moveq	#0,%d1
.Ltrow:
	move.l	%a4,%a0
	move.l	%a3,%a1
	move.l	%d7,%d5
.Ltgroup:
	| long 0: characters 0-4 and the top 2 bits of 5
	moveq	#0,%d0
	move.w	(%a1)+,%d1
	move.b	(%a2,%d1.l),%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	move.w	(%a1)+,%d1
	move.b	(%a2,%d1.l),%d2
	lsl.l	#2,%d0
	move.b	%d2,%d3
	lsr.b	#4,%d3
	or.b	%d3,%d0
	move.l	%d0,(%a0)+
	| long 1: low 4 bits of 5, characters 6-9, top 4 bits of 10
	moveq	#15,%d0
	and.b	%d2,%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	move.w	(%a1)+,%d1
	move.b	(%a2,%d1.l),%d2
	lsl.l	#4,%d0
	move.b	%d2,%d3
	lsr.b	#2,%d3
	or.b	%d3,%d0
	move.l	%d0,(%a0)+
	| long 2: low 2 bits of 10, characters 11-15
	moveq	#3,%d0
	and.b	%d2,%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	lsl.l	#6,%d0
	move.w	(%a1)+,%d1
	or.b	(%a2,%d1.l),%d0
	move.l	%d0,(%a0)+
	dbra	%d5,.Ltgroup
	add.l	%a5,%a4
	add.l	%d4,%a2
	dbra	%d6,.Ltrow
	movem.l	(%sp)+,%d2-%d7/%a2-%a5
	rts

| start.S - entry point and exception entry.
|
| Penguin (the Mac OS booter for Linux/m68k) loads this image, puts the
| bootinfo records (machine, memory, video and device addresses) right
| after it, and jumps to _stext with the MMU off. Penguin 17 loads it at
| 0x1000, where it is linked; in case another booter puts it elsewhere,
| the first thing we do is move it, bootinfo and all, to where it belongs.

	.section .text.head,"ax"
	.globl	_stext
_stext:
	bra.s	1f
	| Bootinfo version table, checked by Penguin before it boots us.
	.long	0x4249561A		| BOOTINFOV_MAGIC
	.long	3, 0x00020000		| MACH_MAC, bootinfo version 2.0
	.long	0
1:	move.w	%sr,%d4			| the state Penguin left, for the boot log
	move.w	#0x2700,%sr		| supervisor mode, interrupts masked
	move.l	%d0,%d3			| Penguin's "Pen2" (or "Pen1": old bootinfo)
#ifdef TEST_RELOC
	| Test builds only: act as if we'd been loaded 2 MB up, as Penguin 19
	| does when the biggest memory bank isn't the one at address 0.
	lea	%pc@(_stext),%a0
	cmp.l	#_stext,%a0
	bne.s	.Ltest_moved
	move.l	#_stext+0x200000,%a1
	move.l	#_end+0x4000,%d0
	sub.l	#_stext,%d0
	lsr.l	#2,%d0
.Ltest_copy:
	move.l	(%a0)+,(%a1)+
	subq.l	#1,%d0
	bne.s	.Ltest_copy
	move.l	#0x0809,%d0
	movec	%d0,%cacr
	move.l	#.Ltest_moved+0x200000,%a0
	jmp	(%a0)
.Ltest_moved:
#endif
	movec	%cacr,%d6
	movec	%vbr,%d5
	lea	%pc@(_stext),%a0	| where we were loaded
	move.l	%a0,%d7
	move.l	#_stext,%a1		| where we are linked
	cmp.l	%a1,%a0
	beq.s	3f
	| Copy image and bootinfo (16 KB is plenty) to the link address.
	move.l	#_end+0x4000,%d0
	sub.l	%a1,%d0
	lsr.l	#2,%d0
	cmp.l	%a1,%a0
	bcs.s	5f
4:	move.l	(%a0)+,(%a1)+		| loaded higher: copy forwards
	subq.l	#1,%d0
	bne.s	4b
	bra.s	2f
5:	lea	(%a0,%d0.l*4),%a0	| loaded lower: copy backwards
	lea	(%a1,%d0.l*4),%a1
6:	move.l	-(%a0),-(%a1)
	subq.l	#1,%d0
	bne.s	6b
2:	move.l	#0x0809,%d0		| the code moved: drop stale instructions
	movec	%d0,%cacr
	move.l	#3f,%a0
	jmp	(%a0)
3:	| Instruction cache on, data cache off: with the MMU off nothing marks
	| the I/O space cache-inhibited.
	move.l	#0x0809,%d0
	movec	%d0,%cacr
	lea	stack_top,%sp
	lea	__bss_start,%a0
	lea	__bss_end,%a1
7:	cmp.l	%a1,%a0
	bcc.s	8f
	clr.l	(%a0)+
	bra.s	7b
8:	move.l	%d7,entry_load
	move.l	%d3,entry_d0
	move.w	%d4,entry_sr
	move.l	%d6,entry_cacr
	move.l	%d5,entry_vbr
	lea	entry_tc,%a0
	pmove	%tc,(%a0)
	pea	_end			| bootinfo records follow the image
	jsr	kmain
9:	stop	#0x2700
	bra.s	9b

	.bss
	.globl	entry_load, entry_sr, entry_cacr, entry_vbr, entry_tc, entry_d0
	.align	4
entry_load:	.skip	4
entry_d0:	.skip	4
entry_cacr:	.skip	4
entry_vbr:	.skip	4
entry_tc:	.skip	4
entry_sr:	.skip	2
	.align	4

	.text
| Every vector points here. Saves the registers C code may clobber and
| passes the exception frame (SR, PC, format/vector word) to exception().
	.globl	exc_common
exc_common:
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	pea	16(%sp)
	jsr	exception
	addq.l	#4,%sp
	movem.l	(%sp)+,%d0-%d1/%a0-%a1
	rte

| int probe8(volatile u8 *addr, u8 *out): read a byte that may not be
| there. Returns 1 and the byte, or 0 if the access bus-errored (an
| empty NuBus or PDS slot times out).
	.globl	probe8
probe8:
	move.w	%sr,-(%sp)
	ori.w	#0x0700,%sr
	movec	%vbr,%a1
	move.l	8(%a1),-(%sp)		| the bus error vector
	move.l	#.Lprobe_fault,8(%a1)
	move.l	%sp,probe_sp
	move.l	10(%sp),%a0
	moveq	#0,%d0
	move.b	(%a0),%d0
	nop
	move.l	14(%sp),%a0
	move.b	%d0,(%a0)
	moveq	#1,%d0
	bra.s	1f
.Lprobe_fault:
	move.l	probe_sp,%sp		| drop the bus error frame
	moveq	#0,%d0
1:	movec	%vbr,%a1
	move.l	(%sp)+,8(%a1)
	move.w	(%sp)+,%sr
	rts

	.bss
	.align	4
probe_sp:
	.skip	4
	.globl	stack_top
	.skip	16384
stack_top:

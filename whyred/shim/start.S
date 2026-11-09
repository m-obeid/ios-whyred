/*
 * whyred shim entry. An arm64 Linux Image header makes ABL treat us as a kernel.
 * ABL enters with MMU and D-cache off, IRQs masked, x0 = DTB pointer.
 */
	.section .text.head, "ax"
	.globl _head
_head:
	b	_start			/* code0 */
	.long	0			/* code1 */
	.quad	0			/* text_offset */
	.quad	_end			/* image_size (linked at 0, so this is the size) */
	.quad	0xa			/* flags: little endian, 4K pages, place anywhere */
	.quad	0			/* res2 */
	.quad	0			/* res3 */
	.quad	0			/* res4 */
	.ascii	"ARM\x64"		/* magic */
	.long	0			/* res5 */

	.text
	.globl _start
_start:
	msr	daifset, #0xf
	mov	x19, x0
	msr	spsel, #1
	adr	x1, _stack_top
	mov	sp, x1
	bl	set_vectors
	/* zero .bss */
	adr	x1, _bss_start
	adr	x2, _bss_end
1:	cmp	x1, x2
	b.hs	2f
	str	xzr, [x1], #8
	b	1b
2:	mov	x0, x19
	bl	shim_main
3:	wfe
	b	3b

/* PSCI CPU_ON target. x0 = context id = cpu slot (0..7). */
	.globl secondary_entry
secondary_entry:
	msr	daifset, #0xf
	mov	x19, x0
	msr	spsel, #1
	adr	x1, _sec_stacks
	add	x1, x1, x0, lsl #12
	add	x1, x1, #0x1000
	mov	sp, x1
	bl	set_vectors
	mov	x0, x19
	bl	secondary_main
4:	wfe
	b	4b

/* Install the vector table at the current EL (EL1, or EL2 if ABL left us there). */
set_vectors:
	adr	x1, vectors
	mrs	x2, CurrentEL
	cmp	x2, #(2 << 2)
	b.ne	5f
	msr	vbar_el2, x1
5:	msr	vbar_el1, x1
	isb
	ret

.macro vent slot
	.balign	0x80
	stp	x29, x30, [sp, #-16]!
	mov	x29, #\slot
	b	exc_common
.endm

	.balign	0x800
vectors:
	vent 0
	vent 1
	vent 2
	vent 3
	vent 4
	vent 5
	vent 6
	vent 7
	vent 8
	vent 9
	vent 10
	vent 11
	vent 12
	vent 13
	vent 14
	vent 15

/* Every exception is fatal here: report on screen and stop. */
exc_common:
	mov	x0, x29
	bl	exc_handler
6:	wfe
	b	6b

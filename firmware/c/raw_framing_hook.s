	.section .framing_entry,"ax",@progbits
	.globl tr_framing_entry
tr_framing_entry:
	ldi:32 #tr_framing_bridge, r12
	jmp @r12

	.section .framing_bridge,"ax",@progbits
	.globl tr_framing_bridge
tr_framing_bridge:
	/* The retained common tail needs both pointers; the earlier hook
	 * bypasses their legacy stack-local initialization. */
	ldi:20 #0xa2850, r2
	st r2, @(r14,-156)
	ldi:20 #0xa27f8, r1
	st r1, @(r14,-132)
	st r0, @-r15
	st r1, @-r15
	st r2, @-r15
	st r3, @-r15
	st r4, @-r15
	st r5, @-r15
	st r6, @-r15
	st r7, @-r15
	st r8, @-r15
	st r9, @-r15
	st r10, @-r15
	st r11, @-r15
	st r12, @-r15
	st r13, @-r15
	st r14, @-r15
	st rp, @-r15
	ld @(r14,-196), r4
	ldi:20 #0xa26a0, r5
	ldi:20 #0xa28af, r6
	ldi:32 #tr_raw_framing, r12
	call @r12
	ldi:20 #0xa2400, r1
	ld @r1, r2
	ldi:20 #0x40000, r3
	or r3, r2
	st r2, @r1
	cmp 0, r4
	beq .Lframing_restore
	ldi:20 #0xa25f4, r1
	ldi:8 #4, r2
	stb r2, @r1
	addn 2, r1
	ldi:8 #3, r2
	stb r2, @r1
.Lframing_restore:
	ld @r15+, rp
	ld @r15+, r14
	ld @r15+, r13
	ld @r15+, r12
	ld @r15+, r11
	ld @r15+, r10
	ld @r15+, r9
	ld @r15+, r8
	ld @r15+, r7
	ld @r15+, r6
	ld @r15+, r5
	ld @r15+, r4
	ld @r15+, r3
	ld @r15+, r2
	ld @r15+, r1
	ld @r15+, r0
	ldi:20 #0xe6bfa, r12
	jmp @r12

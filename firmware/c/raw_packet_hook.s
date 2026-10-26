	.section .raw_entry,"ax",@progbits
	.globl tr_raw_entry
tr_raw_entry:
	ldi:32 #tr_raw_bridge, r12
	jmp @r12

	.section .raw_bridge,"ax",@progbits
	.globl tr_raw_bridge
tr_raw_bridge:
	/* Replay the stack-local initialization bypassed at e51b4. */
	ldi:20 #0xa27f8, r1
	st r1, @(r14,-132)
	/* Preserve the legacy function's live registers and return pointer. */
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
	ldi:20 #0xa28af, r5
	ldi:32 #tr_raw_packet_prepare, r12
	call @r12
	.ifdef RAW_OUTPUT_SOFT_EVENT
	/* The generic-only C scheduler does not wait for disabled AV queues.
	 * Notify the legacy event loop directly when this packet is prepared. */
	ldi:20 #0xa2400, r1
	ld @r1, r2
	ldi:20 #0x40000, r3
	or r3, r2
	st r2, @r1
	.endif
	cmp 0, r4
	beq .Lraw_restore
	ldi:20 #0xa25f4, r1
	ldi:8 #4, r2
	stb r2, @r1
	addn 2, r1
	ldi:8 #3, r2
	stb r2, @r1
.Lraw_restore:
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
	/* Retain the original timestamp, producer progress and DMA scheduler. */
	ldi:20 #0xe6bfa, r12
	jmp @r12

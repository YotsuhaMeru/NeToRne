	/* These leaves originally discarded the entire input packet without
	 * entering the PSI/card parser. Preserve that parser decision, but copy
	 * the original TS through the retained generic DMA state machine.
	 * No new RAM, no global PID class or parser-state changes.
	 */
	.section .routing_copy,"ax",@progbits
	.globl tr_routing_copy
tr_routing_copy:
	/* e18f2 is a delayed branch on the normal parser path. Its slot must
	 * remain this original single-word instruction, never an ldi:20. */
	ldi:8 #6, r13
	ldi:20 #0xa252c, r2
	st r2, @(r14,-328)
	ldi:20 #0xa256c, r1
	ldi:8 #0, r0
	stb r0, @r1
	ldi:20 #0xa2572, r1
	stb r0, @r1
	/* Temporary class2 selects terminal state2 and avoids ES parsing.
	 * Global a2534 remains the original class. The original state2 entry
	 * clears a256c/a2572 too; clearing them here prevents parser re-entry.
	 */
	ldi:8 #2, r0
	st r0, @(r14,-372)
	ldi:20 #0xe1ebe, r12
	jmp @r12
	.rept 6
	nop
	.endr

	/* One shared bridge, reached before each original discard command. */
	.section .routing_early_tei,"ax",@progbits
	ldi:20 #tr_routing_copy, r12
	jmp @r12
	.section .routing_selected_class,"ax",@progbits
	ldi:20 #tr_routing_copy, r12
	jmp @r12
	.section .routing_unknown_class,"ax",@progbits
	ldi:20 #tr_routing_copy, r12
	jmp @r12
	.section .routing_duplicate,"ax",@progbits
	ldi:20 #tr_routing_copy, r12
	jmp @r12
	.section .routing_empty_parser,"ax",@progbits
	ldi:20 #tr_routing_copy, r12
	jmp @r12
	.section .routing_empty_section,"ax",@progbits
	ldi:20 #tr_routing_copy, r12
	jmp @r12

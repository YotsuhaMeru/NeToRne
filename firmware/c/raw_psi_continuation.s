	/* Inline leaf in FR_e0fbc. Entry requires non-PUSI and empty parser
	 * state, verified at e1e20..e1e36. r8 is the zero partial-state byte.
	 * Keep only class2 packets without adaptation; all other cases use
	 * the original full-packet discard leaf. No card/parser calls added.
	 */
	.section .psi_continuation,"ax",@progbits
	.globl tr_psi_continuation
tr_psi_continuation:
	ld @(r14,-372), r0
	cmp 2, r0
	bne .Ldiscard
	ld @(r14,-208), r0
	ldub @r0, r0
	cmp 0, r0
	bne .Ldiscard
	ldi:20 #0xa256c, r2
	stb r8, @r2
	ldi:20 #0xe1eb0, r12
	jmp @r12
.Ldiscard:
	ldi:20 #0xe18f4, r12
	jmp @r12
	nop

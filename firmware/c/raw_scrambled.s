    /* Opt-in raw scrambled variant. Use the existing non-cryptographic
     * transfer without entering key selection or the SC=1 marker writer.
     * Original scheduler, metadata and completion handling stay in place. */
    .section .scrambled_bypass,"ax",@progbits
    .globl tr_scrambled_bypass
tr_scrambled_bypass:
    ldi:20 #0xe0cd4, r12
    jmp @r12

    .section .upstream_entry,"ax",@progbits
    .globl tr_upstream_entry
tr_upstream_entry:
    ldi:20 #tr_raw_upstream, r12
    jmp @r12

    .section .upstream_tail,"ax",@progbits
    .globl tr_upstream_tail
tr_upstream_tail:
    /* Original e133e sets r7, used at e15d8 before redefinition.
     * The C body's reviewed final RET is changed to BRA to this tail.
     * Its own saved RP is already restored; r4 is the continuation. */
    ldi:20 #0xa10f8, r7
    jmp @r4

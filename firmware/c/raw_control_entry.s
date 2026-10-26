/* Tail jumps preserve the caller's RP and use caller-saved r12 only. */
    .section .control_setup_entry,"ax"
    .global tr_raw_setup_entry
tr_raw_setup_entry:
    ldi:20 #tr_raw_setup,r12
    jmp @r12

    .section .control_ring_entry,"ax"
    .global tr_raw_ring_entry
tr_raw_ring_entry:
    ldi:20 #tr_raw_ring_configure,r12
    jmp @r12

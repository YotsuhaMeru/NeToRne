    /* Selected video/audio AF-only leaf (length >=183, generic flag zero).
     * Skip the discard DMA and its discard-state publication. Execute the
     * original CC health check / cached CC update, then copy the complete TS.
     * No parser/key/card logic or memory allocation is added. */
    .section .af_skip_discard,"ax"
    ldi:20 #0xe1c1c,r12
    jmp @r12

    .section .af_finish_copy,"ax"
    /* Keep original JMP:D and its STB cached-CC delay slot at e1c54/e1c56. */
    ldi:20 #0xe18f4,r12

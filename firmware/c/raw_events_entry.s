    .section .compare_entry,"ax"
    .global tr_raw_compare_entry
tr_raw_compare_entry:
    ldi:20 #tr_raw_wrap_compare,r12
    jmp @r12

    .section .completion_entry,"ax"
    .global tr_raw_completion_entry
tr_raw_completion_entry:
    ldi:20 #tr_raw_completion,r12
    jmp @r12

    .section .end_entry,"ax"
    .global tr_raw_end_entry
tr_raw_end_entry:
    ldi:20 #tr_raw_es_end,r12
    jmp @r12

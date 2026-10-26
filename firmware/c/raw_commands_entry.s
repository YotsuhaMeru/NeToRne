    .section .command_entry,"ax"
    .global tr_raw_command_entry
tr_raw_command_entry:
    ldi:20 #tr_raw_commands,r12
    jmp @r12

    .section .transfer_entry,"ax"
    .global tr_raw_transfer_entry
tr_raw_transfer_entry:
    ldi:20 #tr_raw_start_transfers,r12
    jmp @r12

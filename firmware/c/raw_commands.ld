OUTPUT_FORMAT("elf32-fr30")
SECTIONS {
    .command_entry 0xa0000 : SUBALIGN(2) { *(.command_entry) }
    .transfer_entry 0xa0b0e : SUBALIGN(2) { *(.transfer_entry) }
    /* Pinned complete reader ends e4bbc; pinned C transport ends e5038. */
    .command_code 0xe5038 : SUBALIGN(2) {
        *(.command_handler) *(.transfer_handler) *(.text*)
        . = ALIGN(4); *(.rodata*)
    }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(tr_raw_command_entry == 0xa0000, "Command callable entry moved")
ASSERT(tr_raw_transfer_entry == 0xa0b0e, "Transfer callable entry moved")
ASSERT(SIZEOF(.command_entry) == 6, "Command bridge must be six bytes")
ASSERT(SIZEOF(.transfer_entry) == 6, "Transfer bridge must be six bytes")
ASSERT(tr_raw_commands == 0xe5038, "Commands must follow pinned transport")
ASSERT(ADDR(.command_code)+SIZEOF(.command_code) <= 0xe6d62, "Commands overlap output")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

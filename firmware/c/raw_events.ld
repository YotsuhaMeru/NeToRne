OUTPUT_FORMAT("elf32-fr30")
SECTIONS {
    .compare_entry 0xe420a : SUBALIGN(2) { *(.compare_entry) }
    .completion_entry 0xe42a6 : SUBALIGN(2) { *(.completion_entry) }
    .end_entry 0xa0d08 : SUBALIGN(2) { *(.end_entry) }
    /* Pinned C commands end e5b90; original reader tail is abandoned. */
    .event_code 0xe5b90 : SUBALIGN(2) {
        *(.compare_handler) *(.completion_handler) *(.end_handler) *(.text*)
        . = ALIGN(4); *(.rodata*)
    }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(tr_raw_compare_entry == 0xe420a, "Comparator callable entry moved")
ASSERT(tr_raw_completion_entry == 0xe42a6, "Completion callable entry moved")
ASSERT(tr_raw_end_entry == 0xa0d08, "End callable entry moved")
ASSERT(SIZEOF(.compare_entry) == 6, "Comparator bridge must be six bytes")
ASSERT(SIZEOF(.completion_entry) == 6, "Completion bridge must be six bytes")
ASSERT(SIZEOF(.end_entry) == 6, "End bridge must be six bytes")
ASSERT(tr_raw_wrap_compare == 0xe5b90, "Events must follow pinned commands")
ASSERT(ADDR(.event_code)+SIZEOF(.event_code) <= 0xe6d62, "Events overlap output")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

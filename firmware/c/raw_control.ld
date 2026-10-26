OUTPUT_FORMAT("elf32-fr30")
SECTIONS
{
    .control_setup_entry 0xa09c8 : SUBALIGN(2) { *(.control_setup_entry) }
    .control_ring_entry 0xa0eb6 : SUBALIGN(2) { *(.control_ring_entry) }
    /* The complete, pinned C input reader ends at e4bbc. Its abandoned
     * legacy tail is no longer entered; the output scheduler starts e6d62. */
    .control_code 0xe4bbc : SUBALIGN(2) {
        *(.control_setup) *(.control_ring) *(.text*)
        . = ALIGN(4); *(.control_constants) *(.rodata*)
    }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(tr_raw_setup_entry == 0xa09c8, "Setup must retain callable entry")
ASSERT(tr_raw_ring_entry == 0xa0eb6, "Ring configure must retain callable entry")
ASSERT(SIZEOF(.control_setup_entry) == 6, "Setup bridge must be six bytes")
ASSERT(SIZEOF(.control_ring_entry) == 6, "Ring bridge must be six bytes")
ASSERT(tr_raw_setup == 0xe4bbc, "Control starts after complete C reader")
ASSERT(ADDR(.control_code) + SIZEOF(.control_code) <= 0xe6d62, "Control overlaps output scheduler")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

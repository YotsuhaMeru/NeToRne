OUTPUT_FORMAT("elf32-fr30")
SECTIONS {
    .af_skip_discard 0xe1be2 : { *(.af_skip_discard) }
    .af_finish_copy 0xe1c50 : { *(.af_finish_copy) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.af_skip_discard) == 6, "AF discard skip must be six bytes")
ASSERT(SIZEOF(.af_finish_copy) == 4, "AF return target must be four bytes")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

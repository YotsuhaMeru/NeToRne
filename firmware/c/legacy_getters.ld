OUTPUT_FORMAT("elf32-fr30")
SECTIONS
{
    .trc_es 0x802da348 : { *(.trc_es) }
    .trc_pmt 0x802da628 : { *(.trc_pmt) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.trc_es) == 4, "ES getter must fit the verified entry")
ASSERT(SIZEOF(.trc_pmt) == 4, "PMT getter must fit the verified entry")
ASSERT(SIZEOF(.text) == 0, "Unexpected text outside the getters")
ASSERT(SIZEOF(.data) == 0, "Getter must not have writable data")
ASSERT(SIZEOF(.bss) == 0, "Getter must not have BSS")

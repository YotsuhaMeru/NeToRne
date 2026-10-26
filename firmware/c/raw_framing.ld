OUTPUT_FORMAT("elf32-fr30")
SECTIONS
{
    .framing_entry 0xe512a : { *(.framing_entry) }
    .framing_code 0xe5200 : SUBALIGN(2) { *(.framing_bridge) *(.text*) *(.rodata*) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.framing_entry) == 8, "Entry replaces three verified instructions")
ASSERT(SIZEOF(.framing_code) <= 0x600, "Code must fit bypassed PAT parser")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

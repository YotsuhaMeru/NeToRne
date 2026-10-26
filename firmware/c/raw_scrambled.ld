OUTPUT_FORMAT("elf32-fr30")
SECTIONS
{
    .scrambled_bypass 0xe0cb2 : { *(.scrambled_bypass) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.scrambled_bypass) == 6, "Bypass must replace exactly six bytes")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

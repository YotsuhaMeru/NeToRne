OUTPUT_FORMAT("elf32-fr30")
SECTIONS
{
    .raw_entry 0xe51b4 : { *(.raw_entry) }
    .raw_code 0xe5200 : { *(.raw_bridge) *(.text*) *(.rodata*) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.raw_entry) == 8, "Entry must replace three verified instructions")
ASSERT(SIZEOF(.raw_code) <= 0x600, "Code must fit bypassed PAT parser")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

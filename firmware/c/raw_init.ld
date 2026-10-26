OUTPUT_FORMAT("elf32-fr30")
SECTIONS
{
    .raw_init 0xa08b2 : SUBALIGN(2) { *(.text*) *(.rodata*) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(tr_raw_initialize == 0xa08b2, "Initialize must retain callable entry")
ASSERT(SIZEOF(.raw_init) <= 0x116, "Initialize must fit before a09c8")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

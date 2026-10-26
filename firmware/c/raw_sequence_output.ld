OUTPUT_FORMAT("elf32-fr30")
SECTIONS
{
    .raw_output 0xe6d62 : SUBALIGN(2) { *(.text*) *(.wire_crc) . = ALIGN(4); *(.rodata*) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(tr_raw_output == 0xe6d62, "Output must start at original callable entry")
ASSERT(SIZEOF(.raw_output) <= 0x11d4, "Output must fit the original scheduler")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

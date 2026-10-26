OUTPUT_FORMAT("elf32-fr30")
SECTIONS
{
    .input_code 0xe44fc : SUBALIGN(2) { *(.raw_input) *(.text*) *(.rodata*) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.input_code) <= 0x2866, "Input code must fit original FR_e44fc")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

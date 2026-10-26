OUTPUT_FORMAT("elf32-fr30")
SECTIONS
{
    .psi_continuation 0xe1e36 : { *(.psi_continuation) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.psi_continuation) == 34, "Replace only the original empty-parser discard leaf")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

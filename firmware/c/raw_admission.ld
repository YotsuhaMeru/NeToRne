OUTPUT_FORMAT("elf32-fr30")
/* Address is reserved for the standalone instruction interpreter only.
 * It is not a board placement or an entry-point patch. */
SECTIONS
{
    .admission_code 0x500000 : SUBALIGN(2) { *(.text*) *(.rodata*) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(tr_raw_admission == 0x500000, "Standalone test entry moved")
ASSERT(SIZEOF(.admission_code) < 4096, "Standalone leaf grew unexpectedly")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

OUTPUT_FORMAT("elf32-fr30")
SECTIONS
{
    .raw_dispatch 0xe4038 : SUBALIGN(2) { *(.text*) *(.rodata*) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(tr_raw_dispatch == 0xe4038, "Dispatcher must retain its callable entry")
ASSERT(SIZEOF(.raw_dispatch) <= 0x1d2, "Dispatcher must fit before e420a")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

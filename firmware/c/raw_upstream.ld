OUTPUT_FORMAT("elf32-fr30")
/* One tail entry replaces e12ee..e1524; the state gate is outside it. */
SECTIONS
{
    .upstream_entry 0xe12ee : { *(.upstream_entry) }
    .upstream_code 0xe12f4 : SUBALIGN(2) { *(.text*) *(.rodata*) }
    .upstream_tail : { *(.upstream_tail) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(tr_raw_upstream == 0xe12f4, "Upstream body moved")
ASSERT(SIZEOF(.upstream_entry) == 6, "Entry must be six bytes")
ASSERT(SIZEOF(.upstream_tail) == 6, "Tail must restore the live counter pointer")
ASSERT(ADDR(.upstream_tail) + SIZEOF(.upstream_tail) <= 0xe1524, "Upstream overlaps retained counter")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

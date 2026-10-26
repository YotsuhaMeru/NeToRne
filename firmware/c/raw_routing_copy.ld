OUTPUT_FORMAT("elf32-fr30")
SECTIONS
{
    .routing_early_tei 0xe15ae : { *(.routing_early_tei) }
    .routing_selected_class 0xe1738 : { *(.routing_selected_class) }
    .routing_unknown_class 0xe179a : { *(.routing_unknown_class) }
    .routing_copy 0xe18f4 : { *(.routing_copy) }
    .routing_duplicate 0xe1cb0 : { *(.routing_duplicate) }
    .routing_empty_parser 0xe1e36 : { *(.routing_empty_parser) }
    .routing_empty_section 0xe1e92 : { *(.routing_empty_section) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.routing_copy) == 44, "Shared bridge must fit the initial-PUSI/TEI discard leaf")
ASSERT(SIZEOF(.routing_early_tei) == 6, "Early TEI entry extent changed")
ASSERT(SIZEOF(.routing_selected_class) == 6, "Selected-class entry extent changed")
ASSERT(SIZEOF(.routing_unknown_class) == 6, "Unknown-class entry extent changed")
ASSERT(SIZEOF(.routing_duplicate) == 6, "Duplicate-CC entry extent changed")
ASSERT(SIZEOF(.routing_empty_parser) == 6, "Empty-parser entry extent changed")
ASSERT(SIZEOF(.routing_empty_section) == 6, "Empty-section entry extent changed")
ASSERT(SIZEOF(.data) == 0, "No writable data")
ASSERT(SIZEOF(.bss) == 0, "No BSS")

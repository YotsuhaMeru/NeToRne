/* Offline link for code size/relocation checks ONLY. Address 0 is not a
 * deployable load address and this ELF is deliberately not an MB8AC018 image. */
OUTPUT_FORMAT("elf32-fr30")
OUTPUT_ARCH(fr30)
SECTIONS {
    . = 0;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
}

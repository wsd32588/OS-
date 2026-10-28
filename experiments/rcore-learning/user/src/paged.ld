/* Separate ELF fixture: each permission region starts on its own page. */
OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0x40000000;
    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
    } :text
    ASSERT(. <= 0x40001000, "image_demo text exceeds one page")

    . = 0x40001000;
    .rodata : {
        KEEP(*(.rodata.probe))
        *(.rodata .rodata.* .srodata .srodata.*)
    } :rodata
    ASSERT(. <= 0x40002000, "image_demo rodata exceeds one page")

    . = 0x40002000;
    .data : {
        KEEP(*(.data.probe))
        *(.data .data.* .sdata .sdata.*)
    } :data
    .bss ALIGN(16) (NOLOAD) : {
        start_bss = .;
        KEEP(*(.bss.probe))
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        end_bss = .;
    } :data
    ASSERT(. > 0x40003000 && . <= 0x40004000,
           "image_demo data and BSS must occupy two pages")
    /DISCARD/ : { *(.eh_frame .eh_frame_hdr) }
}

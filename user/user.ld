OUTPUT_ARCH(riscv)
ENTRY(_start)
PHDRS {
    image PT_LOAD FLAGS(5);
}
SECTIONS {
    . = 0x40000000;
    __user_image_start = .;
    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
    } : image
    .rodata : {
        *(.rodata .rodata.* .srodata .srodata.*)
    } : image
    __user_image_end = .;
    .data : {
        *(.data .data.* .sdata .sdata.*)
        *(.got .got.* .igot .igot.*)
        *(.tdata .tdata.*)
        *(.init_array .init_array.* .fini_array .fini_array.*)
    } : image
    .bss (NOLOAD) : {
        *(.bss .bss.* .sbss .sbss.* .tbss .tbss.*)
        *(COMMON)
    } : image
    /DISCARD/ : {
        *(.eh_frame .eh_frame.* .comment .note.*)
    }

    ASSERT(_start == 0x40000000, "user entry must be at image start")
    ASSERT(__user_image_end - __user_image_start <= 4096, "user image exceeds one page")
    ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "writeable globals are not supported yet")
}
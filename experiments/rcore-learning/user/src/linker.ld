/* Each chapter 3 application is linked for its own fixed RAM window. */
OUTPUT_ARCH(riscv)
ENTRY(_start)
PROVIDE(USER_BASE_ADDRESS = 0x80400000);

SECTIONS {
    . = USER_BASE_ADDRESS;
    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }
    .rodata ALIGN(16) : {
        *(.rodata .rodata.* .srodata .srodata.*)
    }
    .data ALIGN(16) : {
        *(.data .data.* .sdata .sdata.*)
    }
    .bss ALIGN(16) (NOLOAD) : {
        start_bss = .;
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        end_bss = .;
    }
    ASSERT(. <= USER_BASE_ADDRESS + 0x20000, "user image memory exceeds application window")
    /DISCARD/ : { *(.eh_frame .eh_frame_hdr) }
}

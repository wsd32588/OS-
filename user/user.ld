OUTPUT_ARCH(riscv)
ENTRY(_start)
PHDRS {
    code PT_LOAD FLAGS(5); /*PT_LOAD 数据可加载段*/
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x40000000; /*代码和只读常量*/
    __user_code_start = .;
    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
    } :code
    .rodata : {
        *(.rodata .rodata.* .srodata .srodata.*)
    } :code
    __user_code_end = .;

    . = 0x40003000;
    __user_data_start = .;
    .data : {
        *(.data .data.* .sdata .sdata.*)
    } :data
    __user_data_end = .;
    .bss (NOLOAD) : {
        __user_bss_start = .;
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        __user_bss_end = .;
    } :data

    .unsupported : {
        *(.got .got.* .igot .igot.* .tdata .tdata.* .tbss .tbss.*)
        *(.init_array .init_array.* .fini_array .fini_array.*)
    } :data
    /DISCARD/ : {
        *(.eh_frame .eh_frame.* .comment .note.*)
    }

    ASSERT(_start == 0x40000000, "user entry must be at code start")
    ASSERT(__user_code_end - __user_code_start <= 4096, "user code exceeds one page")
    ASSERT(SIZEOF(.unsupported) == 0, "unsupported user runtime sections")
}

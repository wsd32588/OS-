OUTPUT_ARCH(riscv)
ENTRY(_start)

BASE_ADDRESS = 0x80200000;

PHDRS
{
    /* R = 4 W = 2 X = 1*/
    text    PT_LOAD FLAGS(5); /* RX = 4 + 1, allow read&execute */
    rodata  PT_LOAD FLAGS(4); /* Read-only */
    data    PT_LOAD FLAGS(6); /* RW = 4 + 2, allow read&write*/
}

SECTIONS
{
    . = BASE_ADDRESS;
    __kernel_start = .;
    __text_start = .;

    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
    } :text

    . = ALIGN(4096);
    __text_end = .;
    __rodata_start = .;

    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        *(.eh_frame .eh_frame.*)
    } :rodata

    . = ALIGN(4096);
    __rodata_end = .;
    __data_start = .;

    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
        *(.got .got.*)
    } :data

    __bss_start = .;

    .bss (NOLOAD) : {
        *(.bss.stack)
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
    } :data

    __bss_end = .;

    . = ALIGN(4096);
    __data_end = .;
    __kernel_end = .;
}
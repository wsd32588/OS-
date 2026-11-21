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

    .init_array ALIGN(8) : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } :rodata

    . = ALIGN(4096);
    __rodata_end = .;
    __data_start = .;

    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
        *(.got .got.*)
    } :data

    /*
     * .bss 放进 RW 的 data PT_LOAD：
     *   - 全局/静态对象必须可写；
     *   - NOLOAD 不占 ELF 文件大小，运行时由 entry.S 清零；
     *   - VirtIO 的 descriptor/avail/used/request 静态 DMA 内存也在这里，
     *     所以这段必须可写，并且内核必须对它建立恒等映射，
     *     设备拿到的 guest physical address 才和 CPU 指针数值一致。
     */
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
    __kernel_end = .;   /* PMM 分配起点；必须页对齐 */
}
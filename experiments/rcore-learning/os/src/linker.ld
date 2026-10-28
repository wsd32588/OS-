/* Learning layout: the boot stack is deliberately outside [sbss, ebss). */
OUTPUT_ARCH(riscv)
ENTRY(_start)
BASE_ADDRESS = 0x80200000;

SECTIONS {
    . = BASE_ADDRESS;
    skernel = .;

    .stext = .;
    stext = .;
    .text : {
        KEEP(*(.text.entry))
        . = ALIGN(4096);
        strampoline = .;
        KEEP(*(.text.trampoline))
        trampoline_code_end = .;
        . = ALIGN(4096);
        etrampoline = .;
        *(.text .text.*)
    }
    ASSERT(trampoline_code_end > strampoline, "trampoline code is missing")
    ASSERT(etrampoline - strampoline == 4096, "trampoline must occupy exactly one page")

    . = ALIGN(4096);
    .etext = .;
    .srodata = .;
    etext = .;
    srodata = .;
    .rodata ALIGN(4096) : {
        *(.rodata .rodata.* .srodata .srodata.*)
    }

    . = ALIGN(4096);
    erodata = .;
    sdata = .;
    .data ALIGN(4096) : {
        *(.data .data.* .sdata .sdata.*)
    }
    .bss ALIGN(4096) (NOLOAD) : {
        *(.bss.stack)
        sbss = .;
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        ebss = .;
    }
    ekernel = ALIGN(4096);
    ASSERT(ekernel <= 0x80400000, "kernel overlaps chapter 2 application window")
    /DISCARD/ : { *(.eh_frame .eh_frame_hdr) }
}

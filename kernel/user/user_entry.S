.section .rodata.user_image, "a", @progbits
.balign 4

.global user_image_start
.global user_image_end

user_image_start:
    .incbin "user/build/demo.bin"
user_image_end:

.balign 8

.global user_elf_start
.global user_elf_end

user_elf_start:
    .incbin "user/build/demo.elf"
user_elf_end:
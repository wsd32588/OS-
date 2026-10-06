.section .rodata.user_image, "a", @progbits
.balign 4

.global user_image_start
.global user_image_end

user_image_start:
    .incbin "user/build/demo.bin"
user_image_end:
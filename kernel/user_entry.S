.section .rodata.user_image, "a", @progbits
.balign 4

.option push
.option norvc

.global user_image_start
.global user_image_end

user_image_start:
1:
    addi t0, t0, 1
    j 1b

user_image_end:

.option pop

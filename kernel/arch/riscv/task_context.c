#include "arch/riscv/task_context.h"

#include <stdint.h>

#include "arch/riscv/trap.h"

_Static_assert(_Alignof(RiscvTaskContext) >= 16, "kernel stacks require 16-byte alignment");
_Static_assert(
    TINYOS_KERNEL_STACK_SIZE >= sizeof(struct trap_frame),
    "initial frame must fit the kernel stack"
);

static unsigned long read_gp(void) {
    unsigned long value;
    asm volatile("mv %0, gp" : "=r"(value));
    return value;
}

void riscv_task_context_init(RiscvTaskContext* ctx) {
    const uintptr_t stack_top = (uintptr_t)&ctx->stack[TINYOS_KERNEL_STACK_SIZE];
    struct trap_frame* frame = (struct trap_frame*)(stack_top - sizeof(struct trap_frame));
    unsigned long* words = (unsigned long*)frame;

    for (unsigned long i = 0; i < sizeof(*frame) / sizeof(*words); ++i) {
        words[i] = 0;
    }

    /* Supervisor startup inherits gp; user startup explicitly clears it later. */
    frame->sp = stack_top;
    frame->gp = read_gp();
    ctx->frame = frame;
}

#ifndef TINYOS_RISCV_TASK_CONTEXT_H
#define TINYOS_RISCV_TASK_CONTEXT_H

#define TINYOS_KERNEL_STACK_SIZE 8192U

struct trap_frame;

/* Owns stable stack storage; a live context must not be copied or moved. */
typedef struct RiscvTaskContext {
    struct trap_frame* frame;
    unsigned char stack[TINYOS_KERNEL_STACK_SIZE] __attribute__((aligned(16)));
} RiscvTaskContext;

#ifdef __cplusplus
extern "C" {
#endif

/* Requires unused, permanently placed storage; does not initialize PC/privilege. */
void riscv_task_context_init(RiscvTaskContext* ctx);

#ifdef __cplusplus
}
#endif

#endif

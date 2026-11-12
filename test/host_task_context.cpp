#include "arch/riscv/task_context.h"

#include <cstdint>

#include "arch/riscv/trap.h"

// Host tests model the frame layout; real gp/trap behavior remains a QEMU check.
extern "C" void riscv_task_context_init(RiscvTaskContext* context) {
    const auto stack_top = reinterpret_cast<uintptr_t>(
        context->stack + TINYOS_KERNEL_STACK_SIZE);
    auto* frame = reinterpret_cast<trap_frame*>(stack_top - sizeof(trap_frame));
    *frame = {};
    frame->sp = stack_top;
    context->frame = frame;
}

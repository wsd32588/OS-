    .section .text.trampoline, "ax"
    .balign 4
    .option push
    .option norelax
    .global __trampoline_probe
    .global __trampoline_fault

__trampoline_probe:
    # a0=kernel satp token, a1=user satp token. ra is a low kernel return PC.
    # BATCH18 C1: write a1 into satp, then sfence.vma zero, zero.
    # Replace the next two placeholders. Do not touch sp, call Rust, or ret yet.
    csrw satp, a1
    sfence.vma zero, zero

    # Observe the actual CSR while using the user page table, still in S-mode.
    csrr a2, satp


    # BATCH18 C2: write a0 into satp, then sfence.vma zero, zero.
    # Replace these two placeholders before the low-address ret can execute.
    csrw satp, a0
    sfence.vma zero, zero

    mv a0, a2
    ret

    .balign 4
__trampoline_fault:
    # Fatal only: sscratch contains a live kernel token throughout this probe.
    # Keep the high PC mapped while restoring the kernel table first.
    csrr t0, sscratch
    csrw satp, t0
    sfence.vma zero, zero

    # Only this literal is addressed relative to the high PC, inside this page.
    # Its value is an absolute low address. Do not la/call a low Rust symbol
    # from the high alias; do not switch directly to Bare at a high PC.
    lla t0, .Ltrampoline_low_fault
    ld t0, 0(t0)
    jr t0

    .balign 8
.Ltrampoline_low_fault:
    .dword __paging_fault_entry
    .option pop

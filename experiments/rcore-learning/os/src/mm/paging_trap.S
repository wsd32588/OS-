    .section .text
    .balign 4
    .global __paging_fault_entry
__paging_fault_entry:
    # Fatal S-mode diagnostic only: do not use the interrupted sp or sscratch.
    csrr a0, scause
    csrr a1, sepc
    csrr a2, stval
    # This entry is identity-mapped, so execution can continue after returning
    # to Bare. It cannot recover an arbitrary root that fails to map this entry.
    csrw satp, zero
    sfence.vma zero, zero
    la sp, __paging_fault_stack_top
    tail __paging_fault

    .section .bss.paging_fault_stack
    .balign 16
    .space 8192
__paging_fault_stack_top:

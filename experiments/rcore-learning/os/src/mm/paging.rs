use alloc::boxed::Box;
use core::arch::{asm, global_asm};
use core::ptr::{addr_of_mut, read_volatile, write_volatile};


use super::address::VirtAddr;
use super::frame_allocator;
use super::heap_allocator;
use super::memory_set::MemorySet;
use super::page_table::{PTE_A, PTE_R, PTE_V};

global_asm!(include_str!("paging_trap.S"));

unsafe extern "C" {
    fn __paging_fault_entry();
}

static READ_ONLY_PROBE: [u8; 4] = *b"Sv39";

fn read_satp() -> usize {
    let value: usize;
    // SAFETY: S-mode startup permits reading satp without changing state.
    unsafe { asm!("csrr {}, satp", out(reg) value, options(nomem, nostack)); }
    value
}

fn read_stvec() -> usize {
    let value: usize;
    // SAFETY: S-mode startup permits reading stvec without changing state.
    unsafe { asm!("csrr {}, stvec", out(reg) value, options(nomem, nostack)); }
    value
}

fn read_sscratch() -> usize {
    let value: usize;
    // SAFETY: S-mode startup permits reading sscratch without changing state.
    unsafe { asm!("csrr {}, sscratch", out(reg) value, options(nomem, nostack)); }
    value
}

fn interrupts_disabled() -> bool {
    let sstatus: usize;
    let sie: usize;
    // SAFETY: Read-only startup observation; timer and tasks are not running.
    unsafe {
        asm!("csrr {}, sstatus", "csrr {}, sie", out(reg) sstatus,
            out(reg) sie, options(nomem, nostack));
    }
    sstatus & (1 << 1) == 0 && sie == 0
}

unsafe fn write_stvec(value: usize) {
    // SAFETY: The private caller supplies a mapped direct-mode entry and keeps
    // S interrupts disabled throughout the temporary kernel-only interval.
    unsafe { asm!("csrw stvec, {}", in(reg) value, options(nostack)); }
}

/// # Safety
/// Only this module's closed startup scope may call this: its PC, stack, data,
/// and fault entry must remain accessible; keep the table alive until restoring
/// Bare and completing the flush. No user execution, interrupts or table edits.
unsafe fn write_satp_and_flush(token: usize) {
    // BATCH15 B: one asm! block: csrw satp, {token}; sfence.vma zero, zero.
    // Pass token with in(reg), use options(nostack), and an explicit unsafe block.
    // Do not use nomem/readonly/pure: table stores must precede hardware walks.
    // The same routine is used with token=0 when restoring Bare.
    unsafe {
        core::arch::asm!(
            "csrw satp, {token}",
            "sfence.vma zero, zero",
            token = in(reg) token,
            options(nostack)
        )
    }
}

// Private and confined to run_demo: a normal borrow keeps the owning MemorySet
// alive, and Drop restores hardware before that borrow ends. Do not leak this
// guard or expose it as a general safe activation API.
struct ActiveKernel<'a> {
    memory: &'a MemorySet,
    old_stvec: usize,
}

unsafe fn enter_kernel(memory: &MemorySet) -> Result<ActiveKernel<'_>, &'static str> {
    if read_satp() != 0 {
        return Err("paging scope requires Bare mode");
    }
    if !interrupts_disabled() {
        return Err("paging scope requires disabled S interrupts");
    }
    memory.verify_kernel_mappings()?;
    let old_stvec = read_stvec();
    let fault_entry = __paging_fault_entry as *const () as usize;
    // SAFETY: The complete kernel identity layout was checked above. Both entry
    // and restoration execute at unchanged addresses, with a live owning table.
    unsafe {
        write_stvec(fault_entry);
        write_satp_and_flush(memory.token());
    }
    Ok(ActiveKernel { memory, old_stvec })
}

impl Drop for ActiveKernel<'_> {
    fn drop(&mut self) {
        // SAFETY: No interrupts or user execution intervened. Identity mapping
        // makes PC/SP valid across the return to Bare. Flush before restoring
        // the U-trap vector and before allowing the owning table to be dropped.
        unsafe {
            write_satp_and_flush(0);
            write_stvec(self.old_stvec);
        }
    }
}

#[inline(never)]
fn instruction_probe(value: usize) -> usize {
    core::hint::black_box(value).wrapping_add(1)
}

// The fatal assembly path restores Bare before entering Rust and never returns.
#[unsafe(no_mangle)]
pub extern "C" fn __paging_fault(cause: usize, pc: usize, address: usize) -> ! {
    crate::println!("[paging] fatal S trap: scause={:#x} sepc={:#x} stval={:#x}", cause, pc, address);
    crate::sbi::shutdown(true)
}

pub fn run_demo() -> Result<(), &'static str> {
    assert_eq!(read_satp(), 0);
    assert!(interrupts_disabled());
    let frames_before = frame_allocator::statistics();
    let heap_before = heap_allocator::statistics();
    let old_stvec = read_stvec();
    let old_sscratch = read_sscratch();
    let memory = MemorySet::new_kernel()?;
    let token = memory.token();
    assert_eq!(token >> 60, 8);
    assert_eq!((token >> 44) & 0xffff, 0);
    let mut stack_probe = 0u64;
    let mut heap_probe = Box::new(0u64);
    let stack_pointer = addr_of_mut!(stack_probe);
    let heap_pointer = addr_of_mut!(*heap_probe);
    let read_only = VirtAddr::new(READ_ONLY_PROBE.as_ptr() as usize).expect("read-only probe VA");
    assert_eq!(memory.leaf_flags(read_only), Some(PTE_V | PTE_R | PTE_A));
    let frames_mapped = frame_allocator::statistics();
    let heap_mapped = heap_allocator::statistics();

    // SAFETY: This private scope runs before timer/tasks. The immutable kernel
    // layout includes code, current/diagnostic stacks, heap and table pages;
    // A/D are preset. The guard is neither leaked nor exposed outside this block.
    {
        let active = unsafe { enter_kernel(&memory)? };
        assert_eq!(read_satp(), active.memory.token());
        assert_eq!(read_sscratch(), old_sscratch);
        assert!(interrupts_disabled());
        // SAFETY: Both pointers name independently owned live u64 objects in
        // mapped RW kernel memory, and the static has an R mapping. Volatile
        // accesses ensure these are real loads/stores under the active table.
        unsafe {
            write_volatile(stack_pointer, 0x1234_5678_9abc_def0);
            write_volatile(heap_pointer, 0xfedc_ba98_7654_3210);
            assert_eq!(read_volatile(stack_pointer), 0x1234_5678_9abc_def0);
            assert_eq!(read_volatile(heap_pointer), 0xfedc_ba98_7654_3210);
            assert_eq!(read_volatile(READ_ONLY_PROBE.as_ptr()), b'S');
        }
        assert_eq!(instruction_probe(41), 42);
        crate::println!("[paging] Sv39 active: MODE=8 ASID=0 stack/heap/rodata=true");
        let current_stvec = read_stvec();
        // The one rejection case: an active scope cannot be entered again.
        assert!(unsafe { enter_kernel(&memory) }.is_err());
        assert_eq!(read_satp(), token);
        assert_eq!(read_stvec(), current_stvec);
        assert_eq!(frame_allocator::statistics(), frames_mapped);
        assert_eq!(heap_allocator::statistics(), heap_mapped);
        crate::println!("[paging] reenter=Err state_unchanged=true");
    }

    assert_eq!(read_satp(), 0);
    assert_eq!(read_stvec(), old_stvec);
    assert_eq!(read_sscratch(), old_sscratch);
    assert!(interrupts_disabled());
    // Both probes survive the mode change at their original numeric addresses.
    assert_eq!(stack_probe, 0x1234_5678_9abc_def0);
    assert_eq!(*heap_probe, 0xfedc_ba98_7654_3210);
    drop(heap_probe);
    drop(memory);
    assert_eq!(frame_allocator::statistics(), frames_before);
    assert_eq!(heap_allocator::statistics(), heap_before);
    crate::println!("[paging] Bare restored: stvec/sscratch=true frames/heap=restored");
    Ok(())
}

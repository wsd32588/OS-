//! Closed S-mode startup probe, not a general safe page-table activation API.
use core::arch::{asm, global_asm};
use core::ptr::addr_of;

use super::address::{PAGE_SIZE, PhysAddr, PhysPageNum, VirtAddr};
use super::frame_allocator;
use super::heap_allocator;
use super::memory_set::MemorySet;
use super::page_table::{PTE_A, PTE_R, PTE_V, PTE_X};
use super::user_image::UserImage;

pub(super) const TRAMPOLINE: usize = usize::MAX - PAGE_SIZE + 1;

static IMAGE: &[u8] = include_bytes!("../../../user/target/paged/riscv64gc-unknown-none-elf/release/image_demo.elf");

global_asm!(include_str!("trampoline.S"));

unsafe extern "C" {
    static strampoline: u8;
    static etrampoline: u8;
    fn __trampoline_probe(kernel_token: usize, user_token: usize) -> usize;
    fn __trampoline_fault();
    fn __paging_fault_entry();
}

fn page_bounds() -> Result<(usize, usize), &'static str> {
    // These are linker values, not distinct Rust storage objects. Keep aliased
    // boundary addresses opaque to the optimizer, as in kernel_layout().
    let start = core::hint::black_box(addr_of!(strampoline) as usize);
    let end = core::hint::black_box(addr_of!(etrampoline) as usize);
    if start % PAGE_SIZE != 0 || end % PAGE_SIZE != 0 ||
        end.checked_sub(start) != Some(PAGE_SIZE) {
        return Err("trampoline must occupy one aligned page");
    }
    Ok((start, end))
}

pub(super) fn physical_page() -> Result<PhysPageNum, &'static str> {
    let (start, _) = page_bounds()?;
    Ok(PhysAddr::new(start).ok_or("invalid trampoline physical address")?.floor())
}

pub(super) fn virtual_address(linked: usize) -> Result<usize, &'static str> {
    let (start, end) = page_bounds()?;
    if linked < start || linked >= end {
        return Err("symbol is outside the trampoline page");
    }
    // BATCH18 B: preserve the offset within the linked page:
    // TRAMPOLINE + (linked - start). Bounds were checked before this formula.
    Ok(TRAMPOLINE + (linked - start))
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct CsrState {
    satp: usize,
    stvec: usize,
    sscratch: usize,
    sstatus: usize,
    sie: usize,
}

fn read_state() -> CsrState {
    let satp: usize;
    let stvec: usize;
    let sscratch: usize;
    let sstatus: usize;
    let sie: usize;
    // SAFETY: Single-hart S-mode startup permits these read-only CSR accesses.
    unsafe {
        asm!(
            "csrr {satp}, satp",
            "csrr {stvec}, stvec",
            "csrr {sscratch}, sscratch",
            "csrr {sstatus}, sstatus",
            "csrr {sie}, sie",
            satp = out(reg) satp,
            stvec = out(reg) stvec,
            sscratch = out(reg) sscratch,
            sstatus = out(reg) sstatus,
            sie = out(reg) sie,
            options(nomem, nostack)
        );
    }
    CsrState { satp, stvec, sscratch, sstatus, sie }
}

fn require_startup(state: CsrState) -> Result<(), &'static str> {
    if state.satp != 0 || state.sstatus & 2 != 0 || state.sie != 0 {
        return Err("trampoline probe requires Bare with SIE=0 and sie=0");
    }
    Ok(())
}

unsafe fn write_stvec(value: usize) {
    // SAFETY: The closed caller selects a direct-mode entry accessible in the
    // current and next address spaces, with all S interrupts disabled.
    unsafe { asm!("csrw stvec, {}", in(reg) value, options(nostack)); }
}

unsafe fn write_sscratch(value: usize) {
    // SAFETY: No U trap or concurrent execution uses sscratch in this scope.
    // The temporary high fault entry expects the live kernel satp token here.
    unsafe { asm!("csrw sscratch, {}", in(reg) value, options(nostack)); }
}

unsafe fn write_satp_and_flush(token: usize) {
    // SAFETY: Only the closed guard calls this at an identity-mapped low PC/SP,
    // for its verified kernel table or Bare. Table owners stay live through
    // sfence; omitting nomem keeps table stores ordered before hardware walks.
    unsafe {
        asm!("csrw satp, {token}", "sfence.vma zero, zero",
            token = in(reg) token, options(nostack));
    }
}

fn verify_shared_page(memory: &MemorySet) -> Result<(), &'static str> {
    let physical = physical_page()?.to_addr().as_usize();
    for offset in [0, PAGE_SIZE - 1] {
        let va = VirtAddr::new(TRAMPOLINE + offset).ok_or("invalid trampoline VA")?;
        if memory.translate_va(va).map(PhysAddr::as_usize) != Some(physical + offset) ||
            memory.leaf_flags(va) != Some(PTE_V | PTE_R | PTE_X | PTE_A) {
            return Err("trampoline mapping must share the fixed RX supervisor page");
        }
    }
    Ok(())
}

type Probe = unsafe extern "C" fn(usize, usize) -> usize;

// Never expose or leak this guard. Its two borrows keep every table/data owner
// alive, and no mutable table access is possible during the hardware interval.
struct ProbeScope<'a> {
    kernel: &'a MemorySet,
    user: &'a MemorySet,
    previous: CsrState,
    fault_alias: usize,
    probe: Probe,
}

impl<'a> ProbeScope<'a> {
    /// # Safety
    /// Startup only. The provided assembly must use no stack or data memory
    /// under the user table and restore the kernel token before returning.
    /// Keep this private guard until the probe completes; never forget it.
    unsafe fn enter(kernel: &'a MemorySet, user: &'a MemorySet) -> Result<Self, &'static str> {
        let previous = read_state();
        require_startup(previous)?;
        kernel.verify_kernel_mappings()?;
        verify_shared_page(kernel)?;
        verify_shared_page(user)?;
        let fault_alias = virtual_address(__trampoline_fault as *const () as usize)?;
        let probe_alias = virtual_address(__trampoline_probe as *const () as usize)?;
        // SAFETY: The alias is inside the shared executable page. The symbol
        // implements the declared C ABI; invocation is restricted below.
        let probe = unsafe { core::mem::transmute::<usize, Probe>(probe_alias) };

        // All fallible validation finishes before changing any CSR. The low
        // B15 fatal entry is valid in both Bare and the verified kernel table;
        // only after that table is live do we install the shared high entry.
        // SAFETY: Owners remain borrowed; S interrupts stay off throughout.
        unsafe {
            write_stvec(__paging_fault_entry as *const () as usize);
            write_sscratch(kernel.token());
            write_satp_and_flush(kernel.token());
            write_stvec(fault_alias);
        }
        Ok(Self { kernel, user, previous, fault_alias, probe })
    }

    unsafe fn invoke(&self) -> usize {
        // SAFETY: Both roots share this RX/U0 page. The assembly alone crosses
        // to the user table, reads satp, and returns to the kernel table before
        // any Rust, stack access, or return to a low PC can execute.
        unsafe { (self.probe)(self.kernel.token(), self.user.token()) }
    }
}

impl Drop for ProbeScope<'_> {
    fn drop(&mut self) {
        // SAFETY: The probe returned under the verified kernel identity table.
        // Restore a low fatal vector before Bare: the high alias cannot be
        // fetched in Bare. Complete the flush before restoring the old vector
        // and scratch value, and before either borrowed table can be freed.
        unsafe {
            write_stvec(__paging_fault_entry as *const () as usize);
            write_satp_and_flush(0);
            write_stvec(self.previous.stvec);
            write_sscratch(self.previous.sscratch);
        }
    }
}

pub(super) fn run_demo() -> Result<(), &'static str> {
    let initial = read_state();
    require_startup(initial)?;
    let frames_before = frame_allocator::statistics().ok_or("frame allocator not initialized")?;
    let heap_before = heap_allocator::statistics();
    let mut kernel = MemorySet::new_kernel()?;
    let mut user = UserImage::from_elf(IMAGE)?;
    kernel.map_trampoline()?;
    user.memory.map_trampoline()?;
    verify_shared_page(&kernel)?;
    verify_shared_page(&user.memory)?;
    assert_eq!(kernel.table_frame_count(), 11);
    assert_eq!(kernel.owned_frame_count(), 0);
    assert_eq!(user.memory.table_frame_count(), 5);
    assert_eq!(user.memory.owned_frame_count(), 6);
    let frames_mapped = frame_allocator::statistics().expect("frame allocator");
    let heap_mapped = heap_allocator::statistics();
    assert_eq!(frames_mapped.in_use, frames_before.in_use + 22);
    assert_ne!(kernel.token(), user.memory.token());

    let stack_pointer: usize;
    // SAFETY: Reading sp changes no state and accesses no memory.
    unsafe { asm!("mv {}, sp", out(reg) stack_pointer, options(nomem, nostack)); }
    for raw in [run_demo as *const () as usize, stack_pointer] {
        let va = VirtAddr::new(raw).ok_or("invalid low kernel probe VA")?;
        assert!(user.memory.translate_va(va).is_none());
    }
    crate::println!("[trampoline] shared_page=true RX/U0=true");
    crate::println!("[trampoline] kernel_tables=11 user_tables=5 user_data=6 in_use_delta=22");

    let (_, end) = page_bounds()?;
    let before_rejection = read_state();
    assert!(virtual_address(end).is_err());
    assert_eq!(read_state(), before_rejection);
    assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
    assert_eq!(heap_allocator::statistics(), heap_mapped);
    crate::println!("[trampoline] outside_page=Err unchanged=true");

    {
        // SAFETY: This is the only closed startup call site. Both tables were
        // checked; the guard is not leaked and no Rust runs under the user root.
        let scope = unsafe { ProbeScope::enter(&kernel, &user.memory)? };
        let active = read_state();
        assert_eq!(active.satp, kernel.token());
        assert_eq!(active.stvec, scope.fault_alias);
        assert_eq!(active.sscratch, kernel.token());
        assert_eq!(active.sstatus, initial.sstatus);
        assert_eq!(active.sie, initial.sie);
        // SAFETY: The borrowed tables and shared high code remain live for the
        // whole assembly round trip; invoke restores the kernel root itself.
        let observed = unsafe { scope.invoke() };
        assert_eq!(observed, user.memory.token());
        assert_eq!(read_state(), active);
        assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
        assert_eq!(heap_allocator::statistics(), heap_mapped);
        crate::println!("[trampoline] kernel -> user -> kernel S-mode=true observed_satp=true");
    }

    assert_eq!(read_state(), initial);
    assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
    assert_eq!(heap_allocator::statistics(), heap_mapped);
    drop(user);
    drop(kernel);
    assert_eq!(frame_allocator::statistics(), Some(frames_before));
    assert_eq!(heap_allocator::statistics(), heap_before);
    let after = frame_allocator::statistics().expect("frame allocator");
    crate::println!("[trampoline] restored: satp=Bare stvec/sscratch=true free={} in_use={} heap_restored=true",
        after.free, after.in_use);
    Ok(())
}

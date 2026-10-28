//! One closed ELF execution, before the existing Bare scheduler starts.
use core::arch::{asm, global_asm};

use super::address::{PAGE_SIZE, PhysAddr, VirtAddr};
use super::frame_allocator::{self, FrameTracker};
use super::heap_allocator;
use super::memory_set::MemorySet;
use super::page_table::{PTE_A, PTE_D, PTE_R, PTE_U, PTE_V, PTE_W, PTE_X};
use super::trampoline::{self, TRAMPOLINE};
use super::user_image::UserImage;

pub(super) const TRAP_CONTEXT: usize = TRAMPOLINE - PAGE_SIZE;
static IMAGE: &[u8] = include_bytes!("../../../user/target/paged/riscv64gc-unknown-none-elf/release/image_demo.elf");
global_asm!(include_str!("paged_user.S"));

unsafe extern "C" {
    fn __paged_enter(context: usize, user_token: usize, restore: usize, trap: usize) -> usize;
    fn __paged_restore();
    fn __paged_alltraps();
    fn __paged_return();
    fn __paging_fault_entry();
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) struct PagedTrapContext {
    pub(super) x: [usize; 32],
    sstatus: usize,
    pub(super) sepc: usize,
    kernel_satp: usize,
    kernel_sp: usize,
    trap_handler: usize,
}

const _: () = {
    assert!(core::mem::size_of::<PagedTrapContext>() == 37 * 8);
    assert!(core::mem::offset_of!(PagedTrapContext, sstatus) == 256);
    assert!(core::mem::offset_of!(PagedTrapContext, sepc) == 264);
    assert!(core::mem::offset_of!(PagedTrapContext, kernel_satp) == 272);
    assert!(core::mem::offset_of!(PagedTrapContext, kernel_sp) == 280);
    assert!(core::mem::offset_of!(PagedTrapContext, trap_handler) == 288);
};

impl PagedTrapContext {
    pub(super) fn initial(entry: usize, user_sp: usize, status: usize, kernel_satp: usize) -> Self {
        // BATCH19 A BEGIN
        // Start x=[0;32], x[2]=user_sp, sepc=entry. Clear SPP(8), SIE(1),
        // SPIE(5), SUM(18), MXR(19), FS(13..14), VS(9..10) from status.
        // This closed integer-only run has no interrupt or FP/vector context.
        // Keep the remaining status bits; kernel_sp=0 is filled by assembly
        // from the real continuation stack, trap_handler=__paged_return.
        
        let mut x =  [0usize; 32];
        x[2] = user_sp;
        let sepc = entry;
        let mask = (1usize << 8) | (1 << 1) | (1 << 5) | (1 << 18) | (1 << 19)
            | (3 << 13) | (3 << 9);
        let sstatus = status & !mask;
        let kernel_sp: usize = 0;
        let trap_handler = __paged_return as *const () as usize;
        Self {
            x,
            sstatus,
            sepc,
            kernel_satp,
            kernel_sp,
            trap_handler,
        }
        // BATCH19 A END
    }
}

// Fields drop in this order: both roots first, then the borrowed context page.
// The context is also mapped into the kernel root for the short entry window.
struct PagedSession {
    kernel: MemorySet,
    image: UserImage,
    context: FrameTracker,
}

impl PagedSession {
    fn new(status: usize) -> Result<Self, &'static str> {
        Self::from_elf(IMAGE, status)
    }

    fn from_elf(bytes: &[u8], status: usize) -> Result<Self, &'static str> {
        // Reverse local drop order also protects the borrowed mapping on Err.
        let mut context = frame_allocator::frame_alloc().ok_or("no context frame")?;
        let mut image = UserImage::from_elf(bytes)?;
        let mut kernel = MemorySet::new_kernel()?;
        kernel.map_trampoline()?;
        image.memory.map_trampoline()?;
        // SAFETY: The frame was allocated before both roots. On success the
        // containing object's field order destroys both roots before it.
        unsafe {
            kernel.map_paged_context(context.ppn())?;
            image.memory.map_paged_context(context.ppn())?;
        }
        let initial = PagedTrapContext::initial(image.entry, image.stack_top, status, kernel.token());
        // SAFETY: Aligned exclusive frame, large enough for the repr(C) value.
        // This reference ends before any hardware/assembly can access the page.
        unsafe { context.bytes_mut().as_mut_ptr().cast::<PagedTrapContext>().write(initial); }
        Ok(Self { kernel, image, context })
    }

    fn saved_context(&self) -> PagedTrapContext {
        // SAFETY: Called only while user/assembly is paused, in Bare or after
        // checking the kernel identity root. No page reference survives entry;
        // no outstanding mutable reference remains; the whole value was
        // initialized before entry and has only integer fields.
        unsafe { self.context.bytes().as_ptr().cast::<PagedTrapContext>().read() }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct CsrState {
    satp: usize,
    stvec: usize,
    sscratch: usize,
    sstatus: usize,
    sie: usize,
    sepc: usize,
    scause: usize,
    stval: usize,
}

fn read_state() -> CsrState {
    let (satp, stvec, sscratch, sstatus, sie, sepc, scause, stval);
    // SAFETY: Read-only S-mode CSR inspection.
    unsafe {
        asm!(
            "csrr {satp}, satp", "csrr {stvec}, stvec",
            "csrr {sscratch}, sscratch", "csrr {sstatus}, sstatus",
            "csrr {sie}, sie", "csrr {sepc}, sepc",
            "csrr {scause}, scause", "csrr {stval}, stval",
            satp=out(reg) satp, stvec=out(reg) stvec,
            sscratch=out(reg) sscratch, sstatus=out(reg) sstatus,
            sie=out(reg) sie, sepc=out(reg) sepc,
            scause=out(reg) scause, stval=out(reg) stval,
            options(nomem, nostack)
        );
    }
    CsrState { satp, stvec, sscratch, sstatus, sie, sepc, scause, stval }
}

fn require_startup(state: CsrState) -> Result<(), &'static str> {
    if state.satp != 0 || state.sstatus & 2 != 0 || state.sie != 0 {
        return Err("paged user run requires Bare, SIE=0 and sie=0");
    }
    Ok(())
}

fn validate_entry(memory: &MemorySet, entry: usize) -> Result<(), &'static str> {
    let va = VirtAddr::new(entry).ok_or("noncanonical user entry")?;
    let flags = memory.leaf_flags(va).ok_or("unmapped user entry")?;
    if entry & 1 != 0 || flags & (PTE_V | PTE_U | PTE_X) != PTE_V | PTE_U | PTE_X {
        return Err("user entry must be aligned executable user memory");
    }
    Ok(())
}

fn verify_mappings(session: &PagedSession) -> Result<(), &'static str> {
    session.kernel.verify_kernel_mappings()?;
    validate_entry(&session.image.memory, session.image.entry)?;
    for memory in [&session.kernel, &session.image.memory] {
        for (base, physical, flags) in [
            (TRAMPOLINE, trampoline::physical_page()?.to_addr().as_usize(), PTE_R | PTE_X | PTE_A),
            (TRAP_CONTEXT, session.context.ppn().to_addr().as_usize(), PTE_R | PTE_W | PTE_A | PTE_D),
        ] {
            for offset in [0, PAGE_SIZE - 1] {
                let va = VirtAddr::new(base + offset).ok_or("invalid shared mapping")?;
                if memory.translate_va(va).map(PhysAddr::as_usize) != Some(physical + offset) ||
                    memory.leaf_flags(va) != Some(flags | PTE_V) {
                    return Err("shared supervisor mapping mismatch");
                }
            }
        }
    }
    let stack_pointer: usize;
    // SAFETY: Read-only stack pointer inspection.
    unsafe { asm!("mv {}, sp", out(reg) stack_pointer, options(nomem, nostack)); }
    for raw in [stack_pointer, __paged_return as *const () as usize] {
        let va = VirtAddr::new(raw).ok_or("invalid kernel address")?;
        if session.image.memory.translate_va(va).is_some() {
            return Err("user root unexpectedly maps low kernel stack/code");
        }
    }
    Ok(())
}

// Private, non-leaked exclusive borrow: no Rust references into context/data
// exist while user code or trap assembly writes those pages.
struct ActiveRun<'a> {
    session: &'a mut PagedSession,
    previous: CsrState,
    restore: usize,
    trap: usize,
}

impl<'a> ActiveRun<'a> {
    unsafe fn enter(session: &'a mut PagedSession) -> Result<Self, &'static str> {
        let previous = read_state();
        require_startup(previous)?;
        verify_mappings(session)?;
        let restore = trampoline::virtual_address(__paged_restore as *const () as usize)?;
        let trap = trampoline::virtual_address(__paged_alltraps as *const () as usize)?;
        // All validation precedes the first state change.
        unsafe { activate_kernel(session.kernel.token()); }
        Ok(Self { session, previous, restore, trap })
    }

    fn saved_context(&self) -> PagedTrapContext {
        assert_eq!(read_state().satp, self.session.kernel.token());
        self.session.saved_context()
    }

    fn replace_context(&mut self, context: PagedTrapContext) {
        assert_eq!(read_state().satp, self.session.kernel.token());
        // SAFETY: The verified kernel root identity-maps the entire pool.
        // User execution is paused and this exclusive page borrow ends before
        // invoke; no Rust reference into the context survives the assembly call.
        unsafe {
            self.session.context.bytes_mut().as_mut_ptr()
                .cast::<PagedTrapContext>().write(context);
        }
    }

    unsafe fn invoke(&mut self) -> usize {
        // SAFETY: The checked roots and exclusive context owner remain live.
        // The low bridge restores kernel gp/tp/s0..s11/ra/sp before Rust resumes.
        unsafe { __paged_enter(TRAP_CONTEXT, self.session.image.memory.token(), self.restore, self.trap) }
    }
}

impl Drop for ActiveRun<'_> {
    fn drop(&mut self) {
        // SAFETY: The assembly returned at a low PC on the kernel identity
        // stack. Flush before releasing the owner borrow; restore all CSR state
        // changed by the closed user/trap round trip. SIE was zero on entry.
        unsafe { restore_state(self.previous); }
    }
}

pub(super) fn run_demo() -> Result<(), &'static str> {
    let initial = read_state();
    require_startup(initial)?;
    let frames_before = frame_allocator::statistics().ok_or("frame allocator not initialized")?;
    let heap_before = heap_allocator::statistics();
    let mut session = PagedSession::new(initial.sstatus)?;
    verify_mappings(&session)?;
    assert_eq!(session.kernel.table_frame_count(), 11);
    assert_eq!(session.image.memory.table_frame_count(), 5);
    assert_eq!(session.image.memory.owned_frame_count(), 6);
    let frames_mapped = frame_allocator::statistics().expect("frame allocator");
    let heap_mapped = heap_allocator::statistics();
    assert_eq!(frames_mapped.in_use, frames_before.in_use + 23);
    crate::println!("[paged-user] shared RX/U0=true context RW/U0=true in_use_delta=23");

    // The single rejection performs no allocation and changes no hardware.
    assert!(validate_entry(&session.image.memory, session.image.guard_start).is_err());
    assert_eq!(read_state(), initial);
    assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
    assert_eq!(heap_allocator::statistics(), heap_mapped);
    crate::println!("[paged-user] unmapped_entry=Err unchanged=true");

    let expected_token = session.image.memory.token();
    let kernel_token = session.kernel.token();
    crate::println!("[paged-user] enter U-mode entry={:#x} user_sp={:#x}",
        session.image.entry, session.image.stack_top);
    let observed_token;
    let trapped;
    {
        // SAFETY: One non-interruptible, integer-only startup application. Its
        // first trap ends this demo; no scheduling or return to U is attempted.
        let mut active = unsafe { ActiveRun::enter(&mut session)? };
        observed_token = unsafe { active.invoke() };
        trapped = read_state();
    }
    assert_eq!(read_state(), initial);
    assert_eq!(observed_token, expected_token);
    assert_eq!(trapped.satp, kernel_token);
    assert_eq!(trapped.stvec, __paging_fault_entry as *const () as usize);
    assert_eq!(trapped.scause, 8, "paged ELF must exit through U ecall");
    let context = session.saved_context();
    assert_eq!(context.sstatus & (1 << 8), 0);
    assert_eq!(context.x[17], 93, "this demo accepts only exit");
    assert_eq!(context.x[10], 0, "image_demo must exit successfully");
    assert_eq!(context.sepc, trapped.sepc);
    assert!(context.x[2] >= session.image.stack_bottom && context.x[2] <= session.image.stack_top);
    assert_eq!(context.x[2] % 16, 0);
    let instruction = [0x73, 0x00, 0x00, 0x00];
    for (offset, expected) in instruction.into_iter().enumerate() {
        let va = VirtAddr::new(context.sepc.checked_add(offset).ok_or("ecall PC overflow")?)
            .ok_or("invalid ecall PC")?;
        assert_eq!(session.image.memory.owned_byte(va), Some(expected));
    }
    assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
    assert_eq!(heap_allocator::statistics(), heap_mapped);
    crate::println!("[paged-user] U ecall: id=93 code=0 SPP=0 observed_satp=true");
    drop(session);
    assert_eq!(frame_allocator::statistics(), Some(frames_before));
    assert_eq!(heap_allocator::statistics(), heap_before);
    crate::println!("[paged-user] restored: satp=Bare CSR=true free={} in_use={} heap_restored=true",
        frames_before.free, frames_before.in_use);
    Ok(())
}


// Reuse the private B19 transition scope; syscall policy lives in its own module.
// Each invocation installs a fresh continuation before restoring the saved user.
pub(super) fn run_syscall_demo(bytes: &[u8]) -> Result<(), &'static str> {
    use super::paged_syscall::{self, Outcome};

    let initial = read_state();
    require_startup(initial)?;
    let frames_before = frame_allocator::statistics().ok_or("frame allocator not initialized")?;
    let heap_before = heap_allocator::statistics();
    let mut session = PagedSession::from_elf(bytes, initial.sstatus)?;
    verify_mappings(&session)?;
    assert_eq!(session.kernel.table_frame_count(), 11);
    assert_eq!(session.image.memory.table_frame_count(), 5);
    assert_eq!(session.image.memory.owned_frame_count(), 6);
    assert_eq!(session.image.guard_start, paged_syscall::BAD_BUFFER + 2);
    let expected_token = session.image.memory.token();
    let frames_mapped = frame_allocator::statistics().expect("frame allocator");
    let heap_mapped = heap_allocator::statistics();
    assert_eq!(frames_mapped.in_use, frames_before.in_use + 23);
    crate::println!("[paged-syscall] in_use_delta=23 context/shared=true");
    let mut traps = 0;
    let mut resumes = 0;
    {
        // SAFETY: Same closed, non-interruptible integer-only setup as B19.
        // One verified session stays owned throughout all four user traps.
        let mut active = unsafe { ActiveRun::enter(&mut session)? };
        loop {
            if traps == 4 {
                return Err("paged syscall fixture exceeded four traps");
            }
            let observed = unsafe { active.invoke() };
            let state = read_state();
            let mut context = active.saved_context();
            traps += 1;
            assert_eq!(observed, expected_token);
            assert_eq!(state.satp, active.session.kernel.token());
            assert_eq!(state.stvec, __paging_fault_entry as *const () as usize);
            assert_eq!(state.scause, 8, "fixture must trap through U ecall");
            assert_eq!(state.sie, 0);
            assert_eq!(state.sstatus & 2, 0);
            assert_eq!(context.sstatus & (1 << 8), 0);
            assert_eq!(context.sepc, state.sepc);
            assert!(context.x[2] >= active.session.image.stack_bottom &&
                context.x[2] <= active.session.image.stack_top);
            assert_eq!(context.x[2] % 16, 0);
            for (offset, byte) in [0x73, 0, 0, 0].into_iter().enumerate() {
                let va = VirtAddr::new(context.sepc.checked_add(offset)
                    .ok_or("ecall PC overflow")?).ok_or("invalid ecall PC")?;
                assert_eq!(active.session.image.memory.owned_byte(va), Some(byte));
            }
            let memory = &active.session.image.memory;
            match traps {
                1 => {
                    assert_eq!(context.x[17], 64);
                    assert_eq!(context.x[10], 1);
                    assert_eq!(context.x[12], paged_syscall::RO_MESSAGE.len());
                    let va = VirtAddr::new(context.x[11]).ok_or("invalid rodata buffer")?;
                    assert_eq!(memory.leaf_flags(va), Some(PTE_V | PTE_R | PTE_U | PTE_A));
                }
                2 => {
                    assert_eq!(context.x[17], 64);
                    assert_eq!(context.x[11], paged_syscall::BAD_BUFFER);
                    assert_eq!(context.x[12], 4);
                    // Observe the valid prefix without ever printing it.
                    for (offset, byte) in b"QZ".iter().copied().enumerate() {
                        let va = VirtAddr::new(paged_syscall::BAD_BUFFER + offset)
                            .ok_or("invalid fixture prefix")?;
                        assert_eq!(memory.owned_byte(va), Some(byte));
                    }
                    let mut output = [0xa5; 4];
                    assert!(paged_syscall::copy_readable_user(memory,
                        paged_syscall::BAD_BUFFER, &mut output).is_err());
                    assert_eq!(output, [0xa5; 4]);
                }
                3 => {
                    assert_eq!(context.x[17], 64);
                    assert_eq!(context.x[12], paged_syscall::STACK_MESSAGE.len());
                    let end = context.x[11].checked_add(context.x[12])
                        .ok_or("stack buffer overflow")?;
                    assert!(context.x[11] >= active.session.image.stack_bottom &&
                        end <= active.session.image.stack_top);
                }
                4 => {
                    assert_eq!(context.x[17], 93);
                    assert_eq!(context.x[10], 0, "fixture checks every write result");
                }
                _ => unreachable!(),
            }
            let outcome = paged_syscall::dispatch(memory, &context);
            match outcome {
                Outcome::Return(result) => {
                    let expected = match traps {
                        1 => paged_syscall::RO_MESSAGE.len() as isize,
                        2 => -1,
                        3 => paged_syscall::STACK_MESSAGE.len() as isize,
                        _ => return Err("exit unexpectedly returned"),
                    };
                    assert_eq!(result, expected);
                    let old = context;
                    paged_syscall::prepare_resume(&mut context, result)?;
                    let mut expected_context = old;
                    expected_context.sepc += 4;
                    expected_context.x[10] = result as usize;
                    assert!(context.x == expected_context.x &&
                        context.sstatus == expected_context.sstatus &&
                        context.sepc == expected_context.sepc &&
                        context.kernel_satp == expected_context.kernel_satp &&
                        context.kernel_sp == expected_context.kernel_sp &&
                        context.trap_handler == expected_context.trap_handler);
                    active.replace_context(context);
                    resumes += 1;
                    crate::println!("[paged-syscall] resume {} result={} sepc_plus4=true", resumes, result);
                    if traps == 2 {
                        crate::println!("[paged-syscall] guard_cross=Err output_unchanged=true prefix_not_printed=true");
                    }
                }
                Outcome::Exit(code) => {
                    assert_eq!(traps, 4);
                    assert_eq!(code, 0);
                    break;
                }
            }
            assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
            assert_eq!(heap_allocator::statistics(), heap_mapped);
        }
    }
    assert_eq!(traps, 4);
    assert_eq!(resumes, 3);
    assert_eq!(read_state(), initial);
    assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
    assert_eq!(heap_allocator::statistics(), heap_mapped);
    crate::println!("[paged-syscall] U_ecalls=4 resumes=3 exit=0 SPP=0 observed_satp=true");
    drop(session);
    assert_eq!(frame_allocator::statistics(), Some(frames_before));
    assert_eq!(heap_allocator::statistics(), heap_before);
    crate::println!("[paged-syscall] restored: satp=Bare CSR=true free={} in_use={} heap_restored=true",
        frames_before.free, frames_before.in_use);
    Ok(())
}

// Callers validate a live kernel identity root and keep its owner borrowed.
unsafe fn activate_kernel(token: usize) {
    unsafe {
        asm!(
            "csrw stvec, {fatal}", "csrw satp, {kernel}",
            "sfence.vma zero, zero", "fence.i",
            fatal=in(reg) __paging_fault_entry as *const () as usize,
            kernel=in(reg) token, options(nostack)
        );
    }
}

// Called only at a low kernel PC on an identity-mapped continuation stack.
unsafe fn restore_state(p: CsrState) {
    unsafe {
        asm!(
            "csrw stvec, {fatal}", "csrw satp, {satp}",
            "sfence.vma zero, zero", "csrw sscratch, {scratch}",
            "csrw sepc, {sepc}", "csrw scause, {cause}",
            "csrw stval, {value}", "csrw sie, {sie}",
            "csrw sstatus, {status}", "csrw stvec, {vector}",
            fatal=in(reg) __paging_fault_entry as *const () as usize,
            satp=in(reg) p.satp, scratch=in(reg) p.sscratch,
            sepc=in(reg) p.sepc, cause=in(reg) p.scause, value=in(reg) p.stval,
            sie=in(reg) p.sie, status=in(reg) p.sstatus, vector=in(reg) p.stvec,
            options(nostack)
        );
    }
}

// The B21 driver sees only a startup snapshot, never writable raw CSR fields.
pub(super) struct StartupState(CsrState);

impl StartupState {
    pub(super) fn capture() -> Result<Self, &'static str> {
        let state = read_state();
        require_startup(state)?;
        Ok(Self(state))
    }

    pub(super) fn status(&self) -> usize {
        self.0.sstatus
    }

    pub(super) fn assert_restored(&self) {
        assert_eq!(read_state(), self.0);
    }
}

// Bare or the checked identity root must be active, with the user paused.
// No page reference escapes this complete-value write.
pub(super) fn initialize_context_page(frame: &mut FrameTracker, context: PagedTrapContext) {
    unsafe { frame.bytes_mut().as_mut_ptr().cast::<PagedTrapContext>().write(context); }
}

// Shared kernel event-loop guard. It owns no task and retains no user-page
// reference, so the caller can inspect/update an owned task between invocations.
pub(super) struct PagedRuntime<'a> {
    kernel: &'a MemorySet,
    previous: CsrState,
    restore: usize,
    trap: usize,
    timer: Option<TimerScope>,
}

impl<'a> PagedRuntime<'a> {
    /// # Safety
    /// Kernel is the verified identity root. Every invocation must supply an
    /// owned, supervisor-only context mapped in this root and the chosen user
    /// root, plus a live user token. User/assembly accesses are not concurrent.
    pub(super) unsafe fn enter(kernel: &'a MemorySet) -> Result<Self, &'static str> {
        let previous = read_state();
        require_startup(previous)?;
        kernel.verify_kernel_mappings()?;
        let restore = trampoline::virtual_address(__paged_restore as *const () as usize)?;
        let trap = trampoline::virtual_address(__paged_alltraps as *const () as usize)?;
        unsafe { activate_kernel(kernel.token()); }
        Ok(Self { kernel, previous, restore, trap, timer: None })
    }

    pub(super) unsafe fn invoke(
        &mut self, context_va: usize, image: &UserImage, frame: &FrameTracker,
    ) -> Result<usize, &'static str> {
        if read_state().satp != self.kernel.token() {
            return Err("paged runtime lost its kernel root");
        }
        let first = TRAMPOLINE - PAGE_SIZE;
        if context_va != first && context_va != first - PAGE_SIZE {
            return Err("paged runtime received an invalid context slot");
        }
        let context_va = VirtAddr::new(context_va).ok_or("invalid context VA")?;
        let permissions = PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;
        let physical = frame.ppn().to_addr().as_usize();
        let code_va = VirtAddr::new(TRAMPOLINE).ok_or("invalid trampoline VA")?;
        let code_pa = trampoline::physical_page()?.to_addr().as_usize();
        for memory in [self.kernel, &image.memory] {
            if memory.translate_va(context_va).map(PhysAddr::as_usize) != Some(physical) ||
                memory.leaf_flags(context_va) != Some(permissions) ||
                memory.translate_va(code_va).map(PhysAddr::as_usize) != Some(code_pa) ||
                memory.leaf_flags(code_va) != Some(PTE_V | PTE_R | PTE_X | PTE_A) {
                return Err("paged runtime context or trampoline mapping mismatch");
            }
        }
        // The only user token passed to assembly belongs to this checked image.
        Ok(unsafe { __paged_enter(context_va.as_usize(), image.memory.token(), self.restore, self.trap) })
    }

    pub(super) fn saved_context(&self, frame: &FrameTracker) -> PagedTrapContext {
        assert_eq!(read_state().satp, self.kernel.token());
        // SAFETY: Verified pool identity mapping and paused user/assembly. This
        // temporary shared page reference is consumed before invoke can run.
        unsafe { frame.bytes().as_ptr().cast::<PagedTrapContext>().read() }
    }

    pub(super) fn replace_context(&self, frame: &mut FrameTracker, context: PagedTrapContext) {
        assert_eq!(read_state().satp, self.kernel.token());
        initialize_context_page(frame, context);
    }

    pub(super) fn enable_timer(&mut self) -> Result<(), &'static str> {
        let state = read_state();
        if self.timer.is_some() || state.satp != self.kernel.token() ||
            state.sie != 0 || state.sstatus & 2 != 0 {
            return Err("timer requires the closed startup kernel runtime");
        }
        self.timer = Some(TimerScope::start()?);
        Ok(())
    }

    // Common return checks apply to both asynchronous timers and U ecalls.
    // In particular, a timer PC may name ANY interrupted user instruction.
    pub(super) fn verify_trap_snapshot(
        &self, observed: usize, user_token: usize, context: &PagedTrapContext,
        image: &UserImage, expected_sie: usize,
    ) -> Result<TrapSnapshot, &'static str> {
        let permitted = if self.timer.is_some() { SIE_STIE } else { 0 };
        let state = read_state();
        if expected_sie != permitted || observed != user_token ||
            state.satp != self.kernel.token() ||
            state.stvec != __paging_fault_entry as *const () as usize ||
            state.sie != expected_sie || state.sstatus & 2 != 0 ||
            context.sstatus & (1 << 8) != 0 || context.sepc != state.sepc {
            return Err("paged task returned through an unexpected trap state");
        }
        if context.x[2] < image.stack_bottom || context.x[2] > image.stack_top ||
            context.x[2] % 16 != 0 {
            return Err("paged task saved an invalid user stack");
        }
        Ok(TrapSnapshot { scause: state.scause, stval: state.stval })
    }

    pub(super) fn verify_user_trap(
        &self, observed: usize, user_token: usize, context: &PagedTrapContext,
        image: &UserImage,
    ) -> Result<(), &'static str> {
        let trap = self.verify_trap_snapshot(observed, user_token, context, image, 0)?;
        if trap.scause != 8 {
            return Err("paged task returned through an unexpected trap state");
        }
        for (offset, expected) in [0x73, 0, 0, 0].into_iter().enumerate() {
            let va = VirtAddr::new(context.sepc.checked_add(offset)
                .ok_or("ecall PC overflow")?).ok_or("invalid ecall PC")?;
            if image.memory.owned_byte(va) != Some(expected) {
                return Err("paged task did not stop at ecall");
            }
        }
        Ok(())
    }

}

impl Drop for PagedRuntime<'_> {
    fn drop(&mut self) {
        // Mask and cancel this startup-only timer before restoring CSR/Bare.
        // Even an SBI stop failure must not bypass address-space restoration.
        let stopped = self.timer.take().map(|mut timer| timer.stop());
        unsafe { restore_state(self.previous); }
        if let Some(Err(message)) = stopped {
            panic!("paged timer cleanup failed after restoring CSR/Bare: {}", message);
        }
    }
}


pub(super) const SIE_STIE: usize = 1 << 5;

#[derive(Clone, Copy, Debug)]
pub(super) struct TrapSnapshot {
    pub(super) scause: usize,
    pub(super) stval: usize,
}

// There was no preexisting startup timer deadline to preserve. This scope
// cancels its own event with MAX; the old Bare timer::init later arms anew.
struct TimerScope {
    armed: bool,
}

impl TimerScope {
    fn start() -> Result<Self, &'static str> {
        if !crate::sbi::timer_available() {
            return Err("SBI TIME extension is unavailable");
        }
        crate::timer::set_next_trigger()?;
        // SIE remains zero in the kernel. A supervisor timer is nevertheless
        // eligible while privilege is U; no SPIE/SIE=1 user context is required.
        unsafe { asm!("csrs sie, {}", in(reg) SIE_STIE, options(nostack)); }
        Ok(Self { armed: true })
    }

    fn stop(&mut self) -> Result<(), &'static str> {
        if !self.armed {
            return Ok(());
        }
        unsafe { asm!("csrc sie, {}", in(reg) SIE_STIE, options(nostack)); }
        // An error is reported once; Drop must not retry or panic prematurely.
        self.armed = false;
        crate::sbi::set_timer(u64::MAX)
    }
}

impl Drop for TimerScope {
    fn drop(&mut self) {
        // PagedRuntime normally calls stop and handles its result after CSR
        // restoration. This fallback protects any future early owner change.
        if let Err(message) = self.stop() {
            panic!("paged timer fallback cleanup failed: {}", message);
        }
    }
}

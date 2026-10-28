//! B20 policy: validate user buffers, dispatch writes, and resume the same ELF.
use crate::mm::address::VirtAddr;
use crate::mm::paged_syscall::Outcome::{Exit, Return};

use super::page_table::{PTE_R, PTE_U, PTE_V};
use super::memory_set::MemorySet;
use super::paged_user::{self, PagedTrapContext};

pub(super) const MAX_WRITE: usize = 256;
// Fixed addresses belong only to this ELF fixture's linker layout.
pub(super) const BAD_BUFFER: usize = 0x4000_3ffe;
pub(super) const RO_MESSAGE: &[u8] = b"[paged-syscall] rodata\n";
pub(super) const STACK_MESSAGE: &[u8] = b"[paged-syscall] stack\n";
static IMAGE: &[u8] = include_bytes!("../../../user/target/paged/riscv64gc-unknown-none-elf/release/syscall_demo.elf");

#[derive(Debug, PartialEq, Eq)]
pub(super) enum Outcome {
    Return(isize),
    Exit(isize),
}

pub(super) fn copy_readable_user(
    memory: &MemorySet,
    start: usize,
    output: &mut [u8],
) -> Result<(), &'static str> {
    // BATCH20 A BEGIN
    // Bound output.len() by MAX_WRITE; an empty output succeeds even when
    // start is not canonical. Check start+length for overflow. Before writing
    // output, validate EVERY VA in the range with VirtAddr::new, leaf_flags
    // containing V|U|R, and owned_byte being Some. Only then copy bytes through
    // owned_byte. Never dereference the raw user VA under the kernel root.
    
    if output.is_empty() {
        return Ok(());
    }
    
    let length = output.len();
    if length > MAX_WRITE {
        return Err("slice length over MAX_WRITE");
    }
    start.checked_add(length).ok_or("user range overflow")?;
    let required = PTE_V | PTE_U | PTE_R;

    for i in 0..length {
        let va = VirtAddr::new(start + i)
            .ok_or("invalid user address")?;
        let flags: u64 = memory.leaf_flags(va)
            .ok_or("user address is not mapped")?;
        
        if flags & required != required {
            return  Err("user page is not readable");
        }

        if memory.owned_byte(va).is_none() {
            return Err("user data page is not owned");
        }
    }

    for (i, byte) in output.iter_mut().enumerate() {
        let va = VirtAddr::new(start + i)
            .expect("address already validated");

        *byte = memory.owned_byte(va)
            .expect("owner already validated");
    }
    Ok(())
}

pub(super) fn dispatch(memory: &MemorySet, context: &PagedTrapContext) -> Outcome {
    // BATCH20 B BEGIN
    // x17=id; x10=fd/code, x11=buffer VA, x12=length. write(64) accepts fd=1
    // and at most MAX_WRITE bytes. Use a [0u8; MAX_WRITE] kernel buffer, copy
    // the whole requested range with A, THEN output each byte via SBI. Return
    // length on success or -1 on rejection. exit(93) yields Exit(code as isize);
    // unknown IDs yield Return(-1). A failed write prints no prefix bytes.
    match context.x[17] {
        64 => {
            let fd = context.x[10];
            let va = context.x[11];
            let length = context.x[12];
            
            if fd != 1 || length > MAX_WRITE {
                    return Return(-1);
            }

            let mut buffer = [0u8; MAX_WRITE];
            if copy_readable_user(memory, va, &mut buffer[..length]).is_err() {
                return Return(-1);
            }

            for &byte in &buffer[..length] {
                crate::sbi::console_putchar(byte);
            }

            Return(length as isize)
        }
        93 => Exit(context.x[10] as isize),
        _ => Return(-1),
    }
    // BATCH20 B END
}

pub(super) fn prepare_resume(
    context: &mut PagedTrapContext,
    result: isize,
) -> Result<(), &'static str> {
    // BATCH20 C BEGIN
    // Compute sepc.checked_add(4) first. On overflow return Err without edits;
    // otherwise update only sepc and x10=result as usize. The driver later
    // commits this copied value to the owned context page and resumes it.
    let temp = context.sepc.checked_add(4usize).ok_or("sepc over max size")?;

    context.sepc = temp;
    context.x[10] = result as usize;
    // BATCH20 C END
    Ok(())
}

pub(super) fn run_demo() -> Result<(), &'static str> {
    paged_user::run_syscall_demo(IMAGE)
}

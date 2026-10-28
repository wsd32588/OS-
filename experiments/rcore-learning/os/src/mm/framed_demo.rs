use super::address::{PAGE_SIZE, PhysAddr, VirtAddr};
use super::frame_allocator;
use super::heap_allocator;
use super::memory_set::{MapArea, MemorySet};
use super::page_table::{PTE_A, PTE_D, PTE_R, PTE_U, PTE_V, PTE_W, PTE_X};

const CODE_START: usize = 0x4000_0000;
const CODE_END: usize = CODE_START + 2 * PAGE_SIZE;
const STACK_START: usize = CODE_END + PAGE_SIZE;
const STACK_END: usize = STACK_START + PAGE_SIZE;

fn address(raw: usize) -> VirtAddr {
    VirtAddr::new(raw).expect("demo user VA")
}

fn new_user_layout() -> Result<MemorySet, &'static str> {
    MemorySet::new(&[
        MapArea::new_framed(CODE_START, CODE_END, PTE_R | PTE_X | PTE_U | PTE_A)?,
        MapArea::new_framed(STACK_START, STACK_END, PTE_R | PTE_W | PTE_U | PTE_A | PTE_D)?,
    ])
}

fn check_bare() {
    let satp: usize;
    let sstatus: usize;
    // SAFETY: Read-only S-mode startup observation, before timer/user execution.
    unsafe {
        core::arch::asm!("csrr {}, satp", "csrr {}, sstatus", out(reg) satp,
            out(reg) sstatus, options(nomem, nostack));
    }
    assert_eq!(satp, 0);
    assert_eq!(sstatus & 2, 0);
}

fn check_layout(memory: &MemorySet) {
    assert_eq!(memory.table_frame_count(), 3);
    assert_eq!(memory.owned_frame_count(), 3);
    for raw in [CODE_START, CODE_START + PAGE_SIZE, STACK_START] {
        let va = address(raw);
        let physical = memory.translate_va(va).expect("mapped user page");
        let (pool_start, pool_end) = frame_allocator::pool_bounds().expect("frame pool");
        assert!(pool_start <= physical.as_usize() && physical.as_usize() < pool_end);
        assert_ne!(physical.as_usize(), raw);
        assert_eq!(physical.page_offset(), 0);
        assert_eq!(memory.owned_byte(va), Some(0));
        assert_eq!(memory.owned_byte(address(raw + PAGE_SIZE - 1)), Some(0));
    }
    assert_eq!(memory.leaf_flags(address(CODE_START)), Some(PTE_V | PTE_R | PTE_X | PTE_U | PTE_A));
    assert_eq!(memory.leaf_flags(address(STACK_START)), Some(PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D));
    assert_eq!(memory.translate_va(address(CODE_END)), None);
    assert_eq!(memory.translate_va(address(STACK_END)), None);
}

pub fn run_demo() -> Result<(), &'static str> {
    check_bare();
    let frames_before = frame_allocator::statistics().expect("frame allocator");
    let heap_before = heap_allocator::statistics();
    let mut first = new_user_layout()?;
    let mut second = new_user_layout()?;
    check_layout(&first);
    check_layout(&second);
    assert_eq!(frame_allocator::statistics().expect("frame statistics").in_use, frames_before.in_use + 12);
    assert_ne!(first.token(), second.token());
    for raw in [CODE_START, CODE_START + PAGE_SIZE, STACK_START] {
        assert_ne!(first.translate_va(address(raw)), second.translate_va(address(raw)));
    }

    // Eight bytes cross from the first code page (offset 4093) to the second.
    // This is synthetic initialization data, not an executable application.
    let start = CODE_START + PAGE_SIZE - 3;
    first.copy_data(address(start), b"ABCDEFGH")?;
    second.copy_data(address(start), b"12345678")?;
    for (index, (&left, &right)) in b"ABCDEFGH".iter().zip(b"12345678").enumerate() {
        let va = address(start + index);
        assert_eq!(first.owned_byte(va), Some(left));
        assert_eq!(second.owned_byte(va), Some(right));
        let offset = va.page_offset();
        assert_eq!(first.translate_va(va).map(PhysAddr::page_offset), Some(offset));
        assert_eq!(second.translate_va(va).map(PhysAddr::page_offset), Some(offset));
    }
    assert_eq!(first.owned_byte(address(start - 1)), Some(0));
    assert_eq!(first.owned_byte(address(start + 8)), Some(0));
    crate::println!("[framed] spaces=2 each_tables=3 each_data=3 in_use_delta=12");
    crate::println!("[framed] same_va/different_pa=true cross_page_copy=true isolated=true");

    // The sole rejection: this range crosses the end of code into the guard.
    let frames_live = frame_allocator::statistics();
    let heap_live = heap_allocator::statistics();
    assert!(first.copy_data(address(CODE_END - 2), b"BAD!").is_err());
    assert_eq!(first.owned_byte(address(CODE_END - 2)), Some(0));
    assert_eq!(first.owned_byte(address(CODE_END - 1)), Some(0));
    assert_eq!(first.translate_va(address(CODE_END)), None);
    for (index, &byte) in b"ABCDEFGH".iter().enumerate() {
        assert_eq!(first.owned_byte(address(start + index)), Some(byte));
    }
    assert_eq!(frame_allocator::statistics(), frames_live);
    assert_eq!(heap_allocator::statistics(), heap_live);
    crate::println!("[framed] guard_copy=Err unchanged=true");

    drop(first);
    assert_eq!(frame_allocator::statistics().expect("frame statistics").in_use, frames_before.in_use + 6);
    for (index, &byte) in b"12345678".iter().enumerate() {
        assert_eq!(second.owned_byte(address(start + index)), Some(byte));
    }
    drop(second);
    assert_eq!(frame_allocator::statistics(), Some(frames_before));
    assert_eq!(heap_allocator::statistics(), heap_before);
    check_bare();
    crate::println!("[framed] released: free={} in_use={} heap_restored=true satp=Bare",
        frames_before.free, frames_before.in_use);
    Ok(())
}

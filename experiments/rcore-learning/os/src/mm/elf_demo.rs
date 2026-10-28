use super::address::{PAGE_SIZE, VirtAddr};
use super::elf;
use super::frame_allocator;
use super::heap_allocator;
use super::page_table::{PTE_A, PTE_D, PTE_R, PTE_U, PTE_V, PTE_W, PTE_X};
use super::user_image::UserImage;

static IMAGE: &[u8] = include_bytes!("../../../user/target/paged/riscv64gc-unknown-none-elf/release/image_demo.elf");

fn address(raw: usize) -> VirtAddr {
    VirtAddr::new(raw).expect("demo user VA")
}

fn check_bare() {
    let satp: usize;
    let sstatus: usize;
    // SAFETY: Read-only observation during single-hart S-mode startup.
    unsafe {
        core::arch::asm!("csrr {}, satp", "csrr {}, sstatus", out(reg) satp,
            out(reg) sstatus, options(nomem, nostack));
    }
    assert_eq!(satp, 0);
    assert_eq!(sstatus & 2, 0);
}

pub fn run_demo() -> Result<(), &'static str> {
    check_bare();
    let frames_before = frame_allocator::statistics().expect("frame allocator");
    let heap_before = heap_allocator::statistics();
    let loaded = UserImage::from_elf(IMAGE)?;
    assert_eq!(loaded.entry, 0x4000_0000);
    assert_eq!(loaded.guard_start, 0x4000_4000);
    assert_eq!(loaded.stack_bottom, 0x4000_5000);
    assert_eq!(loaded.stack_top, 0x4000_7000);
    assert_eq!(loaded.stack_top % 16, 0);
    assert_eq!(loaded.memory.table_frame_count(), 3);
    assert_eq!(loaded.memory.owned_frame_count(), 6);
    assert_eq!(frame_allocator::statistics().expect("frame statistics").in_use, frames_before.in_use + 9);

    // Fixed fixture has separate text, rodata, and data+BSS LOAD regions.
    // Read back all file bytes and the zero-filled tail via their owners.
    {
        let parsed = elf::parse(IMAGE)?;
        assert_eq!(parsed.segments.len(), 3);
        for segment in &parsed.segments {
            for (offset, &byte) in segment.data.iter().enumerate() {
                assert_eq!(loaded.memory.owned_byte(address(segment.start + offset)), Some(byte));
            }
            for raw in segment.start + segment.data.len()..segment.memory_end {
                assert_eq!(loaded.memory.owned_byte(address(raw)), Some(0));
            }
        }
        assert!(parsed.segments.iter().any(|segment| segment.memory_end - segment.start > segment.data.len()));
    }
    for (offset, &byte) in b"ELF-RODATA-DEMO!!".iter().enumerate() {
        assert_eq!(loaded.memory.owned_byte(address(0x4000_1000 + offset)), Some(byte));
    }
    for (offset, &byte) in b"ELF-DATA".iter().enumerate() {
        assert_eq!(loaded.memory.owned_byte(address(0x4000_2000 + offset)), Some(byte));
    }
    for raw in (loaded.guard_start..loaded.stack_bottom).step_by(PAGE_SIZE) {
        assert_eq!(loaded.memory.translate_va(address(raw)), None);
    }
    for raw in loaded.stack_bottom..loaded.stack_top {
        assert_eq!(loaded.memory.owned_byte(address(raw)), Some(0));
    }
    let base_flags = PTE_V | PTE_U | PTE_A;
    for (raw, permissions) in [
        (0x4000_0000, PTE_R | PTE_X),
        (0x4000_1000, PTE_R),
        (0x4000_2000, PTE_R | PTE_W | PTE_D),
        (0x4000_3000, PTE_R | PTE_W | PTE_D),
        (loaded.stack_bottom, PTE_R | PTE_W | PTE_D),
        (loaded.stack_top - PAGE_SIZE, PTE_R | PTE_W | PTE_D),
    ] {
        assert_eq!(loaded.memory.leaf_flags(address(raw)), Some(base_flags | permissions));
        let physical = loaded.memory.translate_va(address(raw)).expect("mapped ELF page");
        let (pool_start, pool_end) = frame_allocator::pool_bounds()?;
        assert!(pool_start <= physical.as_usize() && physical.as_usize() < pool_end);
        assert_ne!(physical.as_usize(), raw);
    }
    assert_eq!(loaded.memory.translate_va(address(loaded.stack_top)), None);
    crate::println!("[elf] LOAD=3 entry={:#x} tables=3 data=6 in_use_delta=9", loaded.entry);
    crate::println!("[elf] file_bytes=true bss_zero=true permissions=true");
    crate::println!("[elf] guard={:#x} stack=[{:#x},{:#x}) zero=true",
        loaded.guard_start, loaded.stack_bottom, loaded.stack_top);

    // The sole rejection: an incomplete header must not allocate/map anything.
    let frames_live = frame_allocator::statistics();
    let heap_live = heap_allocator::statistics();
    assert!(UserImage::from_elf(&IMAGE[..63]).is_err());
    assert_eq!(frame_allocator::statistics(), frames_live);
    assert_eq!(heap_allocator::statistics(), heap_live);
    assert_eq!(loaded.memory.owned_byte(address(0x4000_2000)), Some(b'E'));
    crate::println!("[elf] truncated=Err unchanged=true");

    drop(loaded);
    assert_eq!(frame_allocator::statistics(), Some(frames_before));
    assert_eq!(heap_allocator::statistics(), heap_before);
    check_bare();
    crate::println!("[elf] released: free={} in_use={} heap_restored=true satp=Bare",
        frames_before.free, frames_before.in_use);
    Ok(())
}

use alloc::vec::Vec;

use super::address::{PAGE_SIZE, VirtAddr};
use super::elf::{self, ElfImage};
use super::memory_set::{MapArea, MemorySet};
use super::page_table::{PTE_A, PTE_D, PTE_R, PTE_U, PTE_W, PTE_X};

const USER_STACK_SIZE: usize = 2 * PAGE_SIZE;

pub(super) struct UserImage {
    pub memory: MemorySet,
    pub entry: usize,
    pub guard_start: usize,
    pub stack_bottom: usize,
    pub stack_top: usize,
}

struct UserLayout {
    areas: Vec<MapArea>,
    guard_start: usize,
    stack_bottom: usize,
    stack_top: usize,
}

fn page_end(address: usize) -> Result<usize, &'static str> {
    let rounded = address.checked_add(PAGE_SIZE - 1).ok_or("page end overflow")?;
    Ok(rounded & !(PAGE_SIZE - 1))
}

fn segment_permissions(flags: u32) -> u64 {
    // BATCH17 A: parse already validated the ELF PF_X=1/PF_W=2/PF_R=4 bits.
    // Start with PTE_U | PTE_A; translate R/W/X separately to PTE_R/W/X.
    // Add PTE_D only when writable. Do not copy the raw ELF integer into PTE.
    
    let mut flag = PTE_U | PTE_A;
    if flags & 4 != 0 {
        flag |= PTE_R;
    } 
    if flags & 2 != 0 {
        flag |= PTE_W | PTE_D;    
    }
    if flags & 1 != 0 {
        flag |= PTE_X
    }

    flag
}

fn plan_layout(image: &ElfImage<'_>) -> Result<UserLayout, &'static str> {
    // Metadata capacity is infrastructure, not a new allocator exercise.
    let mut areas: Vec<MapArea> = Vec::new();
    areas.try_reserve_exact(image.segments.len() + 1)
        .map_err(|_| "user layout metadata allocation failed")?;

    // BATCH17 B: for each segment, round start down and memory_end up with
    // page_end; push MapArea::new_framed with segment_permissions(flags).
    // Track the MAXIMUM rounded end across all segments (guard_start).
    // stack_bottom = guard_start.checked_add(PAGE_SIZE).ok_or(...)?;
    // stack_top = stack_bottom.checked_add(USER_STACK_SIZE).ok_or(...)?;
    // Add one RWU|A|D Framed stack region and return UserLayout.
    // Do not map [guard_start, stack_bottom). MemorySet::new rejects page
    // overlap later; this limited loader does not merge permissions.
    let mut guard_start = 0usize;

    for segment in &image.segments {
        let start = segment.start & !(PAGE_SIZE - 1);
        let end = page_end(segment.memory_end)?;
        let area =
            MapArea::new_framed(start, end, segment_permissions(segment.flags))?;

        areas.push(area);
        guard_start = guard_start.max(end);
    }

    let stack_bottom = guard_start
        .checked_add(PAGE_SIZE)
        .ok_or("guard page overflowed user address space")?;

    let stack_top = stack_bottom
        .checked_add(USER_STACK_SIZE)
        .ok_or("user stack overflowed user address space")?;

    areas.push(MapArea::new_framed(
        stack_bottom,
        stack_top,
        PTE_R | PTE_W | PTE_U | PTE_A | PTE_D,
    )?);
    Ok(UserLayout {
        areas,
        guard_start,
        stack_bottom,
        stack_top
    })
}

impl UserImage {
    pub(super) fn from_elf(data: &[u8]) -> Result<Self, &'static str> {
        // BATCH17 C: parse(data)?, plan_layout(&image)?, then
        // MemorySet::new(&layout.areas)?. For every segment call copy_data
        // at its ORIGINAL VirtAddr::new(segment.start).ok_or(...)?, using
        // segment.data (exactly p_filesz). Do not copy p_memsz file bytes.
        // New frames are zeroed, so the remaining memory/BSS stays zero.
        // Return Self with the memory, image.entry and layout bounds.
        // No partial image escapes on Err; do not activate satp here.
        let image = elf::parse(data)?;
        let layout = plan_layout(&image)?;
        let mut memory = MemorySet::new(&layout.areas)?;

        for segment in image.segments {
            let va = VirtAddr::new(segment.start)
                .ok_or("fail to create virtual addr")?;
            memory.copy_data(va, segment.data)?;
        }

        Ok(Self{
            memory,
            entry: image.entry,
            guard_start: layout.guard_start,
            stack_bottom: layout.stack_bottom,
            stack_top: layout.stack_top
        })
    }
}

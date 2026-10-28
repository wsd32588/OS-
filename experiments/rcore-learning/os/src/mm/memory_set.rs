use alloc::vec::Vec;
use core::cmp::min;
use core::ptr::addr_of;

use super::address::{PAGE_SIZE, PhysAddr, VirtAddr, VirtPageNum};
use super::frame_allocator::{self, FrameTracker};
use super::heap_allocator;
use super::page_table::{PageTable, PTE_A, PTE_D, PTE_R, PTE_V, PTE_W, PTE_X};

const LOW_SV39_END: usize = 1usize << 38;
const KERNEL_FLAGS: u64 = PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;

unsafe extern "C" {
    static stext: u8;
    static etext: u8;
    static srodata: u8;
    static erodata: u8;
    static sdata: u8;
    static ekernel: u8;
    static boot_stack_lower_bound: u8;
    static boot_stack_top: u8;
}

struct KernelLayout {
    text_start: usize,
    text_end: usize,
    rodata_start: usize,
    rodata_end: usize,
    writable_start: usize,
    kernel_end: usize,
}

fn kernel_layout() -> KernelLayout {
    // Linker boundaries may alias (etext == srodata, erodata == sdata). Separate
    // extern statics otherwise let the optimizer assume distinct objects.
    // Keep their numeric linker values opaque before comparing region bounds.
    fn boundary(symbol: *const u8) -> usize {
        core::hint::black_box(symbol as usize)
    }
    KernelLayout {
        text_start: boundary(addr_of!(stext)),
        text_end: boundary(addr_of!(etext)),
        rodata_start: boundary(addr_of!(srodata)),
        rodata_end: boundary(addr_of!(erodata)),
        writable_start: boundary(addr_of!(sdata)),
        kernel_end: boundary(addr_of!(ekernel)),
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MapType {
    Identical,
    Framed,
}

// A descriptor for an aligned, nonempty low-VA region. It owns no RAM.
#[derive(Clone, Copy, Debug)]
pub struct MapArea {
    start_vpn: VirtPageNum,
    end_vpn: VirtPageNum,
    permissions: u64,
    map_type: MapType,
}

impl MapArea {
    pub fn new(start: usize, end: usize, permissions: u64) -> Result<Self, &'static str> {
        // BATCH14 A: reject start>=end, unaligned endpoints, end>=LOW_SV39_END.
        // Validate with super::page_table::validate_leaf_flags and KERNEL_FLAGS (no U/V).
        // Convert both byte endpoints with VirtAddr::new(...).ok_or(...)?.floor().
        // Keep the end exclusive; this batch does not round or cross the VA hole.
        
        if start >= end ||
            end >= LOW_SV39_END ||
            (start & (PAGE_SIZE - 1)) != 0 ||
            (end & (PAGE_SIZE - 1)) != 0 {
            return Err("illegal map area bounds");
        }

        super::page_table::validate_leaf_flags(permissions)?;
        if permissions & !KERNEL_FLAGS != 0 {
            return Err("unsupported permission bits");
        }

        let start_vpn = VirtAddr::new(start).ok_or("invalid start VA")?.floor();
        let end_vpn = VirtAddr::new(end).ok_or("invalid end VA")?.floor();

        Ok(Self { start_vpn, end_vpn, permissions, map_type: MapType::Identical })
    }

    pub fn new_framed(start: usize, end: usize, permissions: u64) -> Result<Self, &'static str> {
        // BATCH16 A: use Self::new(start, end, permissions & !PTE_U)? to reuse
        // checked bounds and all other permission validation. PTE_U lives in
        // super::page_table. Restore the full permissions and set Framed.
        // Framed means owned physical allocation; it does not force U=1.
        let mut area = Self::new(start, end, permissions & !super::page_table::PTE_U)?;
        // 校验用的权限去掉了 U，但描述里要保存调用者给的完整权限。
        area.permissions = permissions;
        area.map_type = MapType::Framed;
        
        Ok(area)
    }

    fn overlaps(&self, other: &Self) -> bool {
        self.start_vpn.as_usize() < other.end_vpn.as_usize() &&
            other.start_vpn.as_usize() < self.end_vpn.as_usize()
    }

    fn map(&self, page_table: &mut PageTable, data_frames: &mut Vec<FramedPage>) -> Result<(), &'static str> {
        match self.map_type {
            MapType::Identical => self.map_identical(page_table),
            MapType::Framed => self.map_framed(page_table, data_frames),
        }
    }

    fn map_framed(&self, page_table: &mut PageTable, data_frames: &mut Vec<FramedPage>) -> Result<(), &'static str> {
        // BATCH16 B: loop over the same exclusive VPN range as map_identical.
        // For each VPN allocate one frame_allocator::frame_alloc().ok_or(...)?,
        // save its ppn, map that VPN to the ppn with self.permissions, then move
        // FramedPage { vpn, frame } into data_frames. Capacity is reserved by new.
        // On Err the current unmapped frame drops, then the constructor's table
        // drops before the Vec of previously mapped data owners. Return Ok.
        for i in self.start_vpn.as_usize()..self.end_vpn.as_usize() {
            let vpn: VirtPageNum = VirtPageNum::new(i).
                expect("error, illegal page number");
            let frame = frame_allocator::frame_alloc().ok_or("page cleanup failed")?;
            let ppn = frame.ppn();
            page_table.map(vpn, ppn, self.permissions)?;

            data_frames.push(FramedPage { vpn, frame });
        }

        Ok(())
    }

    fn map_identical(&self, page_table: &mut PageTable) -> Result<(), &'static str> {
        // BATCH14 B: loop start_vpn.as_usize()..end_vpn.as_usize().
        // Construct a VPN and PPN with the same numeric page number; use
        // super::address::PhysPageNum::new for the PPN. Then call
        // page_table.map(vpn, ppn, self.permissions)? for each page. Return Ok.
        // A partial failure is cleaned up by MemorySet::new dropping its local
        // table. Never frame_alloc/drop a data owner for an identity region.
        for number in self.start_vpn.as_usize()..self.end_vpn.as_usize() {
            let vpn = match VirtPageNum::new(number) {
                Some(vpn) => vpn,
                None => {
                    return Err("new virpagenum failed");
                }
            };

            let ppn = match super::address::PhysPageNum::new(number) {
                Some(ppn) => ppn,
                                None => {
                    return Err("new phypagenum failed");
                }
            };
            page_table.map(vpn, ppn, self.permissions)?;
        }
        
        Ok(())
    }
}

struct FramedPage {
    vpn: VirtPageNum,
    frame: FrameTracker,
}

fn framed_page_capacity(areas: &[MapArea]) -> Result<usize, &'static str> {
    let mut total = 0usize;
    for area in areas {
        if area.map_type == MapType::Framed {
            let count = area.end_vpn.as_usize() - area.start_vpn.as_usize();
            total = total.checked_add(count).ok_or("framed page count overflow")?;
        }
    }
    Ok(total)
}

// Field order intentionally drops the inactive page table before data owners.
// Identity regions own no RAM; Framed regions have one data owner per VPN.
pub struct MemorySet {
    page_table: PageTable,
    areas: Vec<MapArea>,
    data_frames: Vec<FramedPage>,
}

impl MemorySet {
    pub fn new(areas: &[MapArea]) -> Result<Self, &'static str> {
        // BATCH14 C: check every pair with overlaps before allocating anything.
        // try_reserve_exact areas.len() for a local Vec, then extend_from_slice.
        // Create a local PageTable, call area.map(&mut page_table)? for all areas,
        // and return Self. On Err all local owners drop; no partial set escapes.
        for (index, area) in areas.iter().enumerate() {
            for other in &areas[index + 1..] {
                if area.overlaps(other) {
                    return Err("overlapping regions");
                }
            }
        }
        let mut stored = Vec::new();
        stored
            .try_reserve_exact(areas.len())
            .map_err(|_| "memory set metadata allocation failed")?;
        stored.extend_from_slice(areas);

        let mut data_frames = Vec::new();
        data_frames.try_reserve_exact(framed_page_capacity(areas)?)
            .map_err(|_| "framed owner metadata allocation failed")?;
        // Keep the table last among local owners: early return drops it first.
        // 3. 局部页表，逐个映射
        let mut page_table = PageTable::new()?;
        for area in &stored {
            area.map(&mut page_table, &mut data_frames)?;
        }
        Ok(Self { page_table, areas: stored, data_frames })
    }

    pub fn new_kernel() -> Result<Self, &'static str> {
        // BATCH14 D: get kernel_layout() and frame_allocator::pool_bounds()?.
        // Construct four MapAreas: text RX|A, rodata R|A,
        // [writable_start,kernel_end) RW|A|D, and the frame pool RW|A|D.
        // Pass their temporary array as a slice to Self::new. All regions U=0.
        let layout = kernel_layout();
        let memory_pool = frame_allocator::pool_bounds()?;

        let area1 = MapArea::new(layout.text_start, layout.text_end, PTE_R | PTE_X | PTE_A)?;
        let area2 = MapArea::new(layout.rodata_start, layout.rodata_end, PTE_R | PTE_A)?;
        let area3 = MapArea::new(layout.writable_start, layout.kernel_end, PTE_R | PTE_W | PTE_A | PTE_D)?;
        let area4 = MapArea::new(memory_pool.0, memory_pool.1, PTE_R | PTE_W | PTE_A | PTE_D)?;
        

        Self::new(&[area1, area2, area3, area4])
    }

    pub fn owned_frame_count(&self) -> usize {
        self.data_frames.len()
    }

    pub fn table_frame_count(&self) -> usize {
        self.page_table.table_frame_count()
    }

    fn validate_copy_range(&self, start: VirtAddr, length: usize) -> Result<(), &'static str> {
        if length == 0 {
            return Ok(());
        }
        let begin = start.as_usize();
        let end = begin.checked_add(length).ok_or("copy range overflow")?;
        if !self.areas.iter().any(|area| area.map_type == MapType::Framed &&
            area.start_vpn.to_addr().as_usize() <= begin &&
            end <= area.end_vpn.to_addr().as_usize()) {
            return Err("copy must stay inside one framed region");
        }
        let last = VirtAddr::new(end - 1).ok_or("invalid copy end")?.floor();
        for number in start.floor().as_usize()..=last.as_usize() {
            if !self.data_frames.iter().any(|owner| owner.vpn.as_usize() == number) {
                return Err("copy destination frame is not owned");
            }
        }
        Ok(())
    }

    // Initialization only: Bare, inactive space, no external reader of its pages.
    // Permissions describe future accesses; copying via an owner may fill RXU.
    pub fn copy_data(&mut self, start: VirtAddr, data: &[u8]) -> Result<(), &'static str> {
        // BATCH16 C: validate_copy_range(start, data.len())? BEFORE any write.
        // Loop while copied<data.len(): current VA=start+copied, VPN=floor,
        // offset=page_offset, count=min(PAGE_SIZE-offset, remaining bytes).
        // Find the matching mutable data_frames owner, use its frame.bytes_mut()
        // slice to copy the chunk, then advance copied. Return Ok (empty is OK).
        // Never cast the user VA to a pointer or assume physical pages adjacent.
        self.validate_copy_range(start, data.len())?;

        let mut copied: usize = 0;

        while copied < data.len() {
            let current = VirtAddr::new(start.as_usize() + copied)
                .expect("copy range already validated");
            let vpn = current.floor();
            let offset = current.page_offset();

            let count = min(PAGE_SIZE - offset, data.len() - copied);

            let owner = self.data_frames.iter_mut().find(|df| df.vpn == vpn)
                .ok_or("Page fault: VPN not mapped in data_frames during boot copy")?;

            let target_slice = &mut owner.frame.bytes_mut()[offset..offset + count];
            let source_slice = &data[copied..copied + count];
            target_slice.copy_from_slice(source_slice);          

            copied += count;
        }

        Ok(())
    }

    // Private readback for startup demonstrations; it reads the owning frame,
    // not arbitrary physical memory or the numeric user VA.
    pub(super) fn owned_byte(&self, va: VirtAddr) -> Option<u8> {
        let owner = self.data_frames.iter().find(|owner| owner.vpn == va.floor())?;
        Some(owner.frame.bytes()[va.page_offset()])
    }

    pub(super) fn token(&self) -> usize {
        self.page_table.token()
    }

    pub(super) fn map_trampoline(&mut self) -> Result<(), &'static str> {
        // BATCH18 A: make a VPN from VirtAddr::new(TRAMPOLINE).ok_or(...)?.floor(),
        // get the fixed code PPN with super::trampoline::physical_page()?, and
        // return self.page_table.map(vpn, ppn, PTE_R | PTE_X | PTE_A).
        // TRAMPOLINE is in super::trampoline. Do not set U or allocate a data
        // FrameTracker: the kernel's linked code page outlives these tables.
        // Do not use MapArea::new here; its existing API is for low regions.
        let vaddr = VirtAddr::new(super::trampoline::TRAMPOLINE)
            .ok_or("Failed to create vaddr in map_trampoline")?;
        let vpn = vaddr.floor();

        let ppn = super::trampoline::physical_page()?;

        self.page_table.map(vpn, ppn, PTE_R | PTE_X | PTE_A)    
    }

    /// # Safety
    /// The caller owns this borrowed frame and keeps it alive until BOTH roots
    /// are inactive, flushed and destroyed. This adds no data-page owner.
    pub(super) unsafe fn map_paged_context(
        &mut self,
        ppn: super::address::PhysPageNum,
    ) -> Result<(), &'static str> {
        unsafe { self.map_paged_context_at(super::paged_user::TRAP_CONTEXT, ppn) }
    }

    /// # Safety
    /// The caller owns this frame, and drops every borrowing root before the
    /// frame. Both roots must be inactive/flushed before that destruction.
    pub(super) unsafe fn map_paged_context_at(
        &mut self,
        address: usize,
        ppn: super::address::PhysPageNum,
    ) -> Result<(), &'static str> {
        // This startup experiment reserves only the two pages immediately
        // below the shared trampoline; arbitrary high mappings are forbidden.
        let first = super::trampoline::TRAMPOLINE - PAGE_SIZE;
        let second = first - PAGE_SIZE;
        if address != first && address != second {
            return Err("paged context address is outside the two reserved slots");
        }
        let va = VirtAddr::new(address).ok_or("invalid paged context address")?;
        self.page_table.map(va.floor(), ppn, PTE_R | PTE_W | PTE_A | PTE_D)
    }

    // Validate the complete fixed kernel layout before changing hardware state.
    // This reads an inactive table; it does not make a generic space activatable.
    pub(super) fn verify_kernel_mappings(&self) -> Result<(), &'static str> {
        let layout = kernel_layout();
        let (pool_start, pool_end) = frame_allocator::pool_bounds()?;
        let writable = PTE_R | PTE_W | PTE_A | PTE_D;
        for (start, end, flags) in [
            (layout.text_start, layout.text_end, PTE_R | PTE_X | PTE_A),
            (layout.rodata_start, layout.rodata_end, PTE_R | PTE_A),
            (layout.writable_start, layout.kernel_end, writable),
            (pool_start, pool_end, writable),
        ] {
            for raw in (start..end).step_by(PAGE_SIZE) {
                let va = VirtAddr::new(raw).ok_or("invalid kernel probe VA")?;
                if self.translate_va(va).map(PhysAddr::as_usize) != Some(raw) ||
                    self.leaf_flags(va) != Some(flags | PTE_V) {
                    return Err("kernel identity mapping or permissions missing");
                }
            }
        }
        Ok(())
    }

    pub fn translate_va(&self, va: VirtAddr) -> Option<PhysAddr> {
        self.page_table.translate_va(va)
    }

    pub fn leaf_flags(&self, va: VirtAddr) -> Option<u64> {
        self.page_table.leaf_flags(va)
    }
}

fn assert_identity(memory: &MemorySet, raw: usize, flags: u64) {
    let va = VirtAddr::new(raw).expect("valid identity probe VA");
    assert_eq!(memory.translate_va(va).map(PhysAddr::as_usize), Some(raw));
    assert_eq!(memory.leaf_flags(va), Some(flags | PTE_V));
}

fn check_bare_state() {
    let satp: usize;
    let sstatus: usize;
    // SAFETY: Single-hart S-mode startup; reading these CSRs changes no state.
    unsafe {
        core::arch::asm!("csrr {}, satp", "csrr {}, sstatus",
            out(reg) satp, out(reg) sstatus, options(nomem, nostack));
    }
    assert_eq!(satp, 0);
    assert_eq!(sstatus & (1 << 1), 0);
}

pub fn run_demo() -> Result<(), &'static str> {
    check_bare_state();
    let frames_before = frame_allocator::statistics().expect("initialized frame allocator");
    let heap_before = heap_allocator::statistics();
    let layout = kernel_layout();
    assert_eq!(layout.text_end, layout.rodata_start);
    assert_eq!(layout.rodata_end, layout.writable_start);
    let memory = MemorySet::new_kernel()?;
    assert_eq!(memory.areas.len(), 4);
    // This layout occupies VPN2=2, with L0 tables for VPN1=1 (kernel), 2..7
    // (the pool). Thus 1 root + 1 L1 + 7 L0 = 9 table frames, no data frames.
    assert_eq!(memory.page_table.table_frame_count(), 9);
    assert_eq!(frame_allocator::statistics().expect("frame statistics").in_use,
        frames_before.in_use + 9);
    for area in &memory.areas {
        for number in area.start_vpn.as_usize()..area.end_vpn.as_usize() {
            assert_identity(&memory, number * PAGE_SIZE, area.permissions);
        }
        let end = area.end_vpn.to_addr().as_usize();
        assert_identity(&memory, end - 1, area.permissions);
    }

    let writable = PTE_R | PTE_W | PTE_A | PTE_D;
    assert_identity(&memory, addr_of!(boot_stack_lower_bound) as usize, writable);
    assert_identity(&memory, addr_of!(boot_stack_top) as usize - 1, writable);
    let (heap_start, heap_end) = heap_allocator::heap_bounds();
    assert_identity(&memory, heap_start, writable);
    assert_identity(&memory, heap_end - 1, writable);
    let stack_pointer: usize;
    // SAFETY: Reading the active stack pointer does not access or modify memory.
    unsafe { core::arch::asm!("mv {}, sp", out(reg) stack_pointer, options(nomem, nostack)); }
    assert_identity(&memory, stack_pointer, writable);
    let root_address = memory.page_table.root_ppn().to_addr().as_usize();
    assert_identity(&memory, root_address, writable);
    let (pool_start, pool_end) = frame_allocator::pool_bounds()?;
    assert!(memory.translate_va(VirtAddr::new(layout.kernel_end).expect("kernel end VA")).is_none());
    assert!(memory.translate_va(VirtAddr::new(crate::loader::APP_BASE_ADDRESS).expect("app VA")).is_none());
    assert!(memory.translate_va(VirtAddr::new(pool_end).expect("pool end VA")).is_none());
    crate::println!("[space] areas=4 table_frames=9 identity=true permissions=true");
    crate::println!("[space] boot_stack/heap/page_tables=RW gaps=unmapped satp=Bare");

    // The sole rejection case: overlapping descriptors fail before allocation.
    let before_rejection = frame_allocator::statistics();
    let heap_before_rejection = heap_allocator::statistics();
    let first = MapArea::new(pool_start, pool_start + 2 * PAGE_SIZE, writable)?;
    let second = MapArea::new(pool_start + PAGE_SIZE, pool_start + 3 * PAGE_SIZE, writable)?;
    assert!(MemorySet::new(&[first, second]).is_err());
    assert_eq!(frame_allocator::statistics(), before_rejection);
    assert_eq!(heap_allocator::statistics(), heap_before_rejection);
    assert_identity(&memory, pool_start, writable);
    crate::println!("[space] overlap=Err unchanged=true");

    drop(memory);
    assert_eq!(frame_allocator::statistics(), Some(frames_before));
    assert_eq!(heap_allocator::statistics(), heap_before);
    check_bare_state();
    crate::println!("[space] released: free={} in_use={} heap_restored=true",
        frames_before.free, frames_before.in_use);
    Ok(())
}

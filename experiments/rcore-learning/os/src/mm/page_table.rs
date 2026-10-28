use alloc::vec::Vec;

use super::address::{PAGE_SIZE, PhysAddr, PhysPageNum, VirtAddr, VirtPageNum};
use super::frame_allocator::{self, FrameTracker, frame_alloc};

pub const PTE_V: u64 = 1 << 0;
pub const PTE_R: u64 = 1 << 1;
pub const PTE_W: u64 = 1 << 2;
pub const PTE_X: u64 = 1 << 3;
pub const PTE_U: u64 = 1 << 4;
pub const PTE_A: u64 = 1 << 6;
pub const PTE_D: u64 = 1 << 7;
const LEAF_FLAGS: u64 = PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D;
const PPN_MASK: u64 = (1u64 << 44) - 1;
const PTE_SIZE: usize = 8;
const PTE_COUNT: usize = PAGE_SIZE / PTE_SIZE;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(transparent)]
struct PageTableEntry {
    bits: u64,
}

const _: () = assert!(core::mem::size_of::<PageTableEntry>() == PTE_SIZE);
const _: () = assert!(PTE_COUNT == 512);

impl PageTableEntry {
    fn new(ppn: PhysPageNum, flags: u64) -> Self {
        // BATCH13 A：PPN 占 bits[53:10]，flags 占低位。
        // 调用方要么给已经校验过、包含 V 的叶权限，要么只给 V。
        let ppn_bits: u64 = ppn.as_usize() as u64;
        Self { bits: (ppn_bits << 10) | flags }
    }

    fn ppn(self) -> PhysPageNum {
        // BATCH13 B：右移 10 位、取 44 位掩码，再转换成 PhysPageNum。
        // 掩码后的值由代码自身的不变量保证，不是外部输入。
        let bits = self.bits >> 10;
        let res = bits & PPN_MASK;
        PhysPageNum::new(res as usize).expect("PTE ppn is within 44 bits")
    }

    fn flags(self) -> u64 {
        self.bits & 0xff
    }

    fn is_valid(self) -> bool {
        self.flags() & PTE_V != 0
    }

    fn is_leaf(self) -> bool {
        self.flags() & (PTE_R | PTE_X) != 0
    }
}

pub(super) fn validate_leaf_flags(flags: u64) -> Result<(), &'static str> {
    if flags & !LEAF_FLAGS != 0 || flags & (PTE_R | PTE_X) == 0 ||
        (flags & PTE_W != 0 && flags & PTE_R == 0) {
        return Err("invalid leaf permissions");
    }
    Ok(())
}

// 这里只拥有页表页；被映射的数据页由调用方保持存活。
pub struct PageTable {
    root_ppn: PhysPageNum,
    frames: Vec<FrameTracker>,
}

impl PageTable {
    pub fn new() -> Result<Self, &'static str> {
        let mut frames = Vec::new();
        frames.try_reserve(1).map_err(|_| "page-table metadata allocation failed")?;
        let root = frame_alloc().ok_or("no frame for root page table")?;
        let root_ppn = root.ppn();
        frames.push(root);
        Ok(Self { root_ppn, frames })
    }

    pub fn root_ppn(&self) -> PhysPageNum {
        self.root_ppn
    }

    pub fn token(&self) -> usize {
        // BATCH15 A: satp MODE occupies bits[63:60], ASID bits[59:44], and
        // root PPN bits[43:0]. Select Sv39 (MODE=8), ASID=0; use the page
        // number, not its physical byte address. Encoding does not activate it.
        const MODE_SV39: usize = 8;
        const MODE_SHIFT: usize = 60;
        const ASID_SHIFT: usize = 44;

        (MODE_SV39 << MODE_SHIFT) | (0usize << ASID_SHIFT) | self.root_ppn.as_usize()
    }

    pub fn table_frame_count(&self) -> usize {
        self.frames.len()
    }

    fn read_entry(&self, table: PhysPageNum, index: usize) -> Result<PageTableEntry, &'static str> {
        if index >= PTE_COUNT {
            return Err("page-table index out of range");
        }
        let frame = self.frames.iter().find(|frame| frame.ppn() == table)
            .ok_or("page-table frame is not owned")?;
        let offset = index * PTE_SIZE;
        let bytes = frame.bytes()[offset..offset + PTE_SIZE].try_into().expect("eight-byte PTE");
        Ok(PageTableEntry { bits: u64::from_le_bytes(bytes) })
    }

    fn write_entry(&mut self, table: PhysPageNum, index: usize, entry: PageTableEntry) -> Result<(), &'static str> {
        if index >= PTE_COUNT {
            return Err("page-table index out of range");
        }
        let frame = self.frames.iter_mut().find(|frame| frame.ppn() == table)
            .ok_or("page-table frame is not owned")?;
        let offset = index * PTE_SIZE;
        frame.bytes_mut()[offset..offset + PTE_SIZE].copy_from_slice(&entry.bits.to_le_bytes());
        Ok(())
    }

    fn ensure_leaf_table(&mut self, vpn: VirtPageNum) -> Result<PhysPageNum, &'static str> {
        let indexes = vpn.indexes();
        let mut table = self.root_ppn;
        for depth in 0..2 {
            let entry = self.read_entry(table, indexes[depth])?;
            if entry.is_valid() {
                if entry.flags() != PTE_V {
                    return Err("only V-only intermediate entries are supported");
                }
                table = entry.ppn();
                continue;
            }
            let missing = 2 - depth;
            self.frames.try_reserve(missing).map_err(|_| "page-table metadata allocation failed")?;
            let mut pending: [Option<FrameTracker>; 2] = [None, None];
            // 在发布任何父表项之前，先把缺失的页表页全部申请好。
            // 若分配失败，局部所有者会在不持锁的情况下 drop。
            for slot in pending.iter_mut().take(missing) {
                *slot = Some(frame_alloc().ok_or("no frame for intermediate page table")?);
            }
            for (step, slot) in pending.iter_mut().take(missing).enumerate() {
                let child = slot.take().expect("prepared table frame");
                let child_ppn = child.ppn();
                self.frames.push(child);
                self.write_entry(table, indexes[depth + step], PageTableEntry::new(child_ppn, PTE_V))
                    .expect("owned parent table with a valid index");
                table = child_ppn;
            }
            return Ok(table);
        }
        Ok(table)
    }

    pub fn map(&mut self, vpn: VirtPageNum, ppn: PhysPageNum, flags: u64) -> Result<(), &'static str> {
        // BATCH13 C：先校验 validate_leaf_flags，再改动任何东西；用
        // ensure_leaf_table 找到最终页表；读 indexes[2]，拒绝已有效的表项，
        // 然后写入 PageTableEntry::new(ppn, flags | PTE_V)。
        // 不要释放数据页：本方法只记录它的页号。
        
        validate_leaf_flags(flags)?;

        let table_ppn = self.ensure_leaf_table(vpn)?;
        let index = vpn.indexes()[2];
        let entry = self.read_entry(table_ppn, index)?;

        if entry.is_valid() {
            return Err("virtual page is already mapped");
        }

        self.write_entry(
            table_ppn,
            index,
            PageTableEntry::new(ppn, flags | PTE_V),
        )
    }

    // 只读检查，用于区域权限校验；支持的中间项（仅 V）和 4KiB 叶项
    // 与 translate_va 保持一致。
    pub fn leaf_flags(&self, va: VirtAddr) -> Option<u64> {
        let indexes = va.floor().indexes();
        let mut table = self.root_ppn;
        for index in indexes.iter().take(2) {
            let entry = self.read_entry(table, *index).ok()?;
            if !entry.is_valid() || entry.flags() != PTE_V {
                return None;
            }
            table = entry.ppn();
        }
        let leaf = self.read_entry(table, indexes[2]).ok()?;
        if !leaf.is_valid() || !leaf.is_leaf() ||
            (leaf.flags() & PTE_W != 0 && leaf.flags() & PTE_R == 0) {
            return None;
        }
        Some(leaf.flags())
    }

    pub fn translate_va(&self, va: VirtAddr) -> Option<PhysAddr> {
        // BATCH13 D：从根开始走 indexes，每一步用 read_entry(...).ok()?；
        // 拒绝 V=0；深度 0/1 要求中间项仅 V，并跟随子表 PPN。
        // 深度 2 要求叶项且满足 W=>R，然后把物理页首
        // 与 va.page_offset() 相加。不分配内存，也不按 VA 解引用。
        
        let indexes = va.floor().indexes();
        let mut table = self.root_ppn();
        for depth in 0..2 {
            let entry = self.read_entry(table, indexes[depth]).ok()?;

            if !entry.is_valid() || entry.flags() != PTE_V {
                return None; 
            }
            table = entry.ppn();
        }

        let leaf = self.read_entry(table, indexes[2]).ok()?;
        if !leaf.is_valid() || !leaf.is_leaf() {
            return None;
        }
        if leaf.flags() & PTE_W != 0 && leaf.flags() & PTE_R == 0 {
            return None;
        }

        let physical = leaf.ppn().to_addr().as_usize() + va.page_offset();
        PhysAddr::new(physical)     // 正好返回 Option<PhysAddr>，顺带做 56 位范围检查
    }
}

pub fn run_demo() {
    let before = frame_allocator::statistics().expect("initialized frame allocator");
    let sample = PhysPageNum::new(0x12345).expect("sample PPN");
    let encoded = PageTableEntry::new(sample, PTE_V | PTE_R | PTE_W);
    assert_eq!(encoded.bits, 0x048d_1407);
    assert_eq!(encoded.ppn(), sample);
    assert_eq!(encoded.flags(), 7);

    let first = frame_alloc().expect("first data frame");
    let second = frame_alloc().expect("second data frame");
    let first_va = VirtAddr::new(0x4000_1234).expect("first demo VA");
    let second_va = VirtAddr::new(0x4000_2567).expect("second demo VA");
    let missing_va = VirtAddr::new(0x4000_3000).expect("unmapped demo VA");
    let flags = PTE_R | PTE_W | PTE_U | PTE_A | PTE_D;
    let mut table = PageTable::new().expect("root page table");
    table.map(first_va.floor(), first.ppn(), flags).expect("first mapping");
    table.map(second_va.floor(), second.ppn(), flags).expect("second mapping");
    assert_eq!(table.table_frame_count(), 3);
    assert_eq!(frame_allocator::statistics().expect("frame statistics").in_use, before.in_use + 5);
    let expected_first = first.ppn().to_addr().as_usize() + first_va.page_offset();
    let expected_second = second.ppn().to_addr().as_usize() + second_va.page_offset();
    assert_eq!(table.translate_va(first_va).map(PhysAddr::as_usize), Some(expected_first));
    assert_eq!(table.translate_va(second_va).map(PhysAddr::as_usize), Some(expected_second));
    crate::println!("[pt] root_ppn={:#x} table_frames={}", table.root_ppn().as_usize(), table.table_frame_count());
    crate::println!("[pt] va={:#x} -> pa={:#x}", first_va.as_usize(), expected_first);

    let mapped = frame_allocator::statistics().expect("frame statistics");
    assert_eq!(table.translate_va(missing_va), None);
    assert!(table.map(first_va.floor(), second.ppn(), flags).is_err());
    assert_eq!(table.translate_va(first_va).map(PhysAddr::as_usize), Some(expected_first));
    assert_eq!(table.table_frame_count(), 3);
    assert_eq!(frame_allocator::statistics(), Some(mapped));
    crate::println!("[pt] unmapped=None duplicate=Err unchanged=true");

    drop(table);
    assert_eq!(frame_allocator::statistics().expect("frame statistics").in_use, before.in_use + 2);
    drop(first);
    drop(second);
    assert_eq!(frame_allocator::statistics(), Some(before));
    crate::println!("[pt] released: free={} in_use={}", before.free, before.in_use);
}

use alloc::vec::Vec;
use core::{ ptr::addr_of};
use spin::Mutex;

use super::address::{PAGE_SIZE, PhysAddr, PhysPageNum};
use crate::loader::{APP_BASE_ADDRESS, APP_COUNT, APP_SIZE_LIMIT};

// 针对当前 virt/128MiB 布局预留的小页池，位于 DTB 之下。
const RAM_START: usize = 0x8000_0000;
const RAM_END: usize = 0x8800_0000;
const FRAME_POOL_END: usize = 0x8100_0000;

unsafe extern "C" {
    static ekernel: u8;
}

struct StackFrameAllocator {
    start: usize,
    current: usize,
    end: usize,
    recycled: Vec<usize>,
}

impl StackFrameAllocator {
    fn new(start: PhysPageNum, end: PhysPageNum) -> Result<Self, &'static str> {
        let start = start.as_usize();
        let end = end.as_usize();
        if start >= end {
            return Err("empty or reversed frame pool");
        }
        let mut recycled = Vec::new();
        // 在拿帧锁之前预留容量；回收路径不能再扩容这个 Vec。
        recycled.try_reserve_exact(end - start).map_err(|_| "frame metadata allocation failed")?;
        Ok(Self { start, current: start, end, recycled })
    }

    fn alloc(&mut self) -> Option<PhysPageNum> {
        // BATCH12 A：先弹出 recycled；否则返回 current 并前进一格；
        // 若 current == end 则返回 None。永远不要返回作为上界的 end 页。
        match self.recycled.pop() {
            Some(ppn) => {
                return PhysPageNum::new(ppn)
            },
            None => {
                if self.current == self.end {
                    return None
                }
                let saved = self.current;
                self.current += 1;
                return PhysPageNum::new(saved);
            }    
        };
    }

    fn dealloc(&mut self, ppn: PhysPageNum) -> Result<(), &'static str> {
        // BATCH12 B：拒绝 ppn < start、ppn >= current 或已在 recycled 中的页。
        // 所有检查都在修改之前完成；然后压入它的 usize 并返回 Ok。
        let number = ppn.as_usize();
        if number < self.start {
            return Err("page does not belong to this pool");
        } else if number >= self.current {
            return  Err("beyond the range that has been sequentially allocated,
            including pages that have not yet been allocated");
        } else if self.recycled.contains(&number) {
            return Err("page already recycled");
        };

        self.recycled.push(number);
        Ok(())
    }

    fn statistics(&self) -> FrameStatistics {
        let total = self.end - self.start;
        let free = self.end - self.current + self.recycled.len();
        FrameStatistics { total, free, in_use: total - free }
    }
}

static FRAME_ALLOCATOR: Mutex<Option<StackFrameAllocator>> = Mutex::new(None);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FrameStatistics {
    pub total: usize,
    pub free: usize,
    pub in_use: usize,
}

pub fn pool_bounds() -> Result<(usize, usize), &'static str> {
    let application_bytes = APP_COUNT.checked_mul(APP_SIZE_LIMIT).ok_or("application range overflow")?;
    let application_end = APP_BASE_ADDRESS.checked_add(application_bytes).ok_or("application range overflow")?;
    let reserved_end = (addr_of!(ekernel) as usize).max(application_end);
    // 按物理字节边界向上取整，而不是对 VirtAddr 做跨 canonical 区的向上取整。
    let start = reserved_end.checked_add(PAGE_SIZE - 1).ok_or("frame range overflow")? & !(PAGE_SIZE - 1);
    if start < RAM_START || start >= FRAME_POOL_END || FRAME_POOL_END > RAM_END || FRAME_POOL_END % PAGE_SIZE != 0 {
        return Err("frame pool outside the configured RAM layout");
    }
    Ok((start, FRAME_POOL_END))
}

pub fn init() -> Result<(), &'static str> {
    if FRAME_ALLOCATOR.lock().is_some() {
        return Err("frame allocator already initialized");
    }
    let (start, end) = pool_bounds()?;
    let start = PhysAddr::new(start).ok_or("invalid pool start")?.floor();
    let end = PhysAddr::new(end).ok_or("invalid pool end")?.floor();
    let allocator = StackFrameAllocator::new(start, end)?;
    let mut global = FRAME_ALLOCATOR.lock();
    if global.is_some() {
        return Err("frame allocator already initialized");
    }
    *global = Some(allocator);
    Ok(())
}

pub fn statistics() -> Option<FrameStatistics> {
    let global = FRAME_ALLOCATOR.lock();
    global.as_ref().map(StackFrameAllocator::statistics)
}

// 它拥有一个页。复制数值形式的 PPN 并不会复制所有权。
pub struct FrameTracker {
    ppn: PhysPageNum,
}

impl FrameTracker {
    fn new(ppn: PhysPageNum) -> Self {
        let mut frame = Self { ppn };
        frame.bytes_mut().fill(0);
        frame
    }

    pub fn ppn(&self) -> PhysPageNum {
        self.ppn
    }

    pub fn bytes(&self) -> &[u8; PAGE_SIZE] {
        let address = self.ppn.to_addr().as_usize();
        // SAFETY: 自有 RAM 页在 Bare 下可直接访问，或已验证当前内核表
        // 对整个页池采用恒等映射。引用绑定在 self 上，期间用户/汇编暂停，
        // 没有设备或其他别名并发修改这个页；引用不得跨越用户恢复。
        unsafe { &*(address as *const [u8; PAGE_SIZE]) }
    }

    pub fn bytes_mut(&mut self) -> &mut [u8; PAGE_SIZE] {
        let address = self.ppn.to_addr().as_usize();
        // SAFETY: 只有 frame_alloc 会构造这个所有者，且针对一块独占分配、
        // 对齐、位于内核/应用/DTB 之外的 RAM 页。在 Bare 或已验证的页池
        // 恒等映射下，PA 数值可直接访问。引用受独占借用约束，且用户/汇编
        // 暂停、没有设备或其他别名并发访问；引用不得跨越用户恢复。
        unsafe { &mut *(address as *mut [u8; PAGE_SIZE]) }
    }
}

impl Drop for FrameTracker {
    fn drop(&mut self) {
        // BATCH12 C：调用一次 frame_dealloc(self.ppn)。返回 Err 说明内部
        // 所有权不变量被破坏，直接 panic。frame_dealloc 在返回前已经结束
        // 它的锁守卫；本方法内不要再次获取这把锁。
        if let Err(message) = frame_dealloc(self.ppn) {
            panic!("frame deallocation failed: {}", message);
        }
    }
}

pub fn frame_alloc() -> Option<FrameTracker> {
    let ppn = {
        let mut global = FRAME_ALLOCATOR.lock();
        global.as_mut()?.alloc()?
    };
    // 页清零和所有者构造都在帧锁释放之后进行。
    Some(FrameTracker::new(ppn))
}

fn frame_dealloc(ppn: PhysPageNum) -> Result<(), &'static str> {
    let mut global = FRAME_ALLOCATOR.lock();
    global.as_mut().ok_or("frame allocator not initialized")?.dealloc(ppn)
}

pub fn run_demo() {
    let (start, end) = pool_bounds().expect("valid frame pool");
    let before = statistics().expect("initialized frame allocator");
    assert_eq!(before.total, (end - start) / PAGE_SIZE);
    assert_eq!(before.free, before.total);
    assert_eq!(before.in_use, 0);
    crate::println!("[frame] region [{:#x}, {:#x}) pages={}", start, end, before.total);

    let mut first = frame_alloc().expect("first frame");
    let mut second = frame_alloc().expect("second frame");
    let first_ppn = first.ppn();
    assert_eq!(first_ppn.to_addr().as_usize(), start);
    assert_eq!(second.ppn().to_addr().as_usize(), start + PAGE_SIZE);
    assert_ne!(first_ppn, second.ppn());
    assert!(first.bytes_mut().iter().all(|&byte| byte == 0));
    assert!(second.bytes_mut().iter().all(|&byte| byte == 0));
    assert_eq!(statistics().expect("frame statistics").in_use, 2);
    first.bytes_mut().fill(0xa5);
    crate::println!("[frame] allocated ppn={:#x}, {:#x}", first_ppn.as_usize(), second.ppn().as_usize());

    drop(first);
    let mut reused = frame_alloc().expect("recycled frame");
    assert_eq!(reused.ppn(), first_ppn);
    assert!(reused.bytes_mut().iter().all(|&byte| byte == 0));
    crate::println!("[frame] reused ppn={:#x} cleared=true", reused.ppn().as_usize());
    drop(reused);
    drop(second);

    let after = statistics().expect("frame statistics");
    assert_eq!(after, before);
    crate::println!("[frame] released: free={} in_use={}", after.free, after.in_use);
}

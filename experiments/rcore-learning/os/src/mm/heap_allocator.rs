use buddy_system_allocator::LockedHeap;
use core::mem::{align_of, size_of};
use core::ptr::{addr_of, addr_of_mut};

pub const KERNEL_HEAP_SIZE: usize = 256 * 1024;
const DEMO_VALUES: usize = 512;

#[repr(C, align(4096))]
struct HeapSpace {
    bytes: [u8; KERNEL_HEAP_SIZE],
}

#[global_allocator]
static HEAP_ALLOCATOR: LockedHeap = LockedHeap::empty();

// 普通 BSS 区域：先被 clear_bss 清零，之后由分配器独占管理。
static mut HEAP_SPACE: HeapSpace = HeapSpace { bytes: [0; KERNEL_HEAP_SIZE] };
static mut HEAP_INITIALIZED: bool = false;

const _: () = assert!(size_of::<HeapSpace>() == KERNEL_HEAP_SIZE);
const _: () = assert!(align_of::<HeapSpace>() == 4096);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct HeapStatistics {
    pub requested_bytes: usize,
    pub allocated_bytes: usize,
    pub total_bytes: usize,
}

pub fn heap_bounds() -> (usize, usize) {
    let start = addr_of_mut!(HEAP_SPACE) as usize;
    (start, start + KERNEL_HEAP_SIZE)
}

pub fn is_initialized() -> bool {
    // SAFETY：单 hart 启动阶段，SIE=0，不可能有并发堆初始化。
    unsafe { addr_of!(HEAP_INITIALIZED).read() }
}

pub fn statistics() -> HeapStatistics {
    let allocator = HEAP_ALLOCATOR.lock();
    HeapStatistics {
        requested_bytes: allocator.stats_alloc_user(),
        allocated_bytes: allocator.stats_alloc_actual(),
        total_bytes: allocator.stats_total_bytes(),
    }
    // 锁在这里释放，然后调用方才去分配、drop 或打印。
}

pub fn init_heap() -> Result<(), &'static str> {
    // BATCH10 A：先拒绝已初始化的堆，再去碰分配器；
    // 取得堆起始地址，调用 HEAP_ALLOCATOR.lock().init(start, size)，
    // 然后用 addr_of_mut! 把 HEAP_INITIALIZED 置为 true 并返回 Ok(())。
    // 需要成立的安全前提：BSS 中永久存活且对齐的内存、分配器独占管理、
    // 只在 clear_bss 与 trap::init 之后、任何动态分配之前初始化一次。
    if is_initialized() {
       return Err(" heap already initialized"); 
    }

    let (start, end) = heap_bounds();
    unsafe {
        HEAP_ALLOCATOR.lock().init(start, end - start);
        addr_of_mut!(HEAP_INITIALIZED).write(true);
    }

    Ok(())
}

fn demo_allocations() {
    // BATCH10 B：使用 alloc::boxed::Box 和 alloc::vec::Vec。创建 Box<usize>(42)
    // 和装有 0..DEMO_VALUES 的 Vec<usize>，让两者同时存活，调用
    // report_live_allocations(boxed.as_ref(), values.as_slice(), values.capacity())，
    // 然后 drop 两者。这些操作期间不要持有分配器锁。

    use alloc::boxed::Box;
    use alloc::vec::Vec;
    let boxed = Box::new(42usize);
    let mut values: Vec<usize> = Vec::new();
    for i in 0..DEMO_VALUES {
        values.push(i);
    }

    report_live_allocations(
        boxed.as_ref(),
        values.as_slice(),
        values.capacity()
    );
    
    drop(values);
    drop(boxed);
}

fn allocation_is_in_heap(address: usize, length: usize) -> bool {
    let (start, end) = heap_bounds();
    let allocation_end = match address.checked_add(length) {
        Some(value) => value,
        None => return false,
    };
    start <= address && allocation_end <= end
}

fn report_live_allocations(boxed: &usize, values: &[usize], capacity: usize) {
    assert_eq!(*boxed, 42);
    assert_eq!(values.len(), DEMO_VALUES);
    assert!(capacity >= values.len());
    for (index, value) in values.iter().enumerate() {
        assert_eq!(*value, index);
    }
    let box_address = boxed as *const usize as usize;
    let vector_address = values.as_ptr() as usize;
    let vector_bytes = capacity.checked_mul(size_of::<usize>()).expect("capacity overflow");
    assert_eq!(box_address % align_of::<usize>(), 0);
    assert_eq!(vector_address % align_of::<usize>(), 0);
    assert!(allocation_is_in_heap(box_address, size_of::<usize>()));
    assert!(allocation_is_in_heap(vector_address, vector_bytes));
    let live = statistics();
    assert_eq!(live.requested_bytes, size_of::<usize>() + vector_bytes);
    assert!(live.allocated_bytes >= live.requested_bytes);
    crate::println!("[heap] box={} vec_len={} capacity={} requested={} actual={}",
        boxed, values.len(), capacity, live.requested_bytes, live.allocated_bytes);
}

pub fn run_demo() {
    assert!(is_initialized());
    let (start, end) = heap_bounds();
    let before = statistics();
    assert_eq!(before.total_bytes, KERNEL_HEAP_SIZE);
    assert_eq!(before.requested_bytes, 0);
    assert_eq!(before.allocated_bytes, 0);
    crate::println!("[heap] region [{:#x}, {:#x}) bytes={}", start, end, KERNEL_HEAP_SIZE);
    demo_allocations();
    let after = statistics();
    assert_eq!(after, before);
    crate::println!("[heap] released: requested={} actual={}", after.requested_bytes, after.allocated_bytes);
}

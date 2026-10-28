use std::ffi::{c_int, c_uint, c_ulong, c_void};
use tinyos_trial::pmm::{FreeError, Page, Pmm, PAGE_SIZE};
use tinyos_trial::sv39;

// Unsafe is confined to the host test bridge. C accesses live, aligned pools
// owned by this test; the experimental Rust library itself forbids unsafe code.
extern "C" {
    fn pmm_init(start: *const c_void, end: *const c_void) -> c_int;
    fn pmm_alloc_page() -> *mut c_void;
    fn pmm_free_page(page: *mut c_void) -> c_int;
    fn pmm_available_pages() -> c_ulong;
    fn sv39_virtual_address_is_canonical(address: u64) -> c_int;
    fn sv39_vpn_index(address: u64, level: c_uint) -> c_uint;
    fn sv39_make_pte(address: u64, flags: u64) -> u64;
    fn sv39_pte_physical_address(pte: u64) -> u64;
    fn sv39_pte_flags(pte: u64) -> u64;
    fn sv39_pte_is_valid(pte: u64) -> c_int;
    fn sv39_pte_is_leaf(pte: u64) -> c_int;
}

fn next_random(state: &mut u32) -> u32 {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    *state
}

fn compare_sv39(address: u64, flags: u64) {
    // These C functions only operate on integer arguments and have no memory preconditions.
    unsafe {
        assert_eq!(
            sv39::is_canonical(address),
            sv39_virtual_address_is_canonical(address) != 0
        );
        for level in 0..5 {
            assert_eq!(
                sv39::vpn_index(address, level),
                sv39_vpn_index(address, level)
            );
        }
        let pte = sv39::make_pte(address, flags);
        assert_eq!(pte, sv39_make_pte(address, flags), "PTE encoding");
        assert_eq!(sv39::physical_address(pte), sv39_pte_physical_address(pte));
        assert_eq!(sv39::flags(address), sv39_pte_flags(address));
        assert_eq!(sv39::is_valid(address), sv39_pte_is_valid(address) != 0);
        assert_eq!(sv39::is_leaf(address), sv39_pte_is_leaf(address) != 0);
    }
}

#[test]
fn sv39_boundaries_and_20000_c_comparisons() {
    assert!(sv39::is_canonical(0x3fffffffff));
    assert!(!sv39::is_canonical(0x4000000000));
    assert!(sv39::is_canonical(0xffffffc000000000));
    assert!(!sv39::is_canonical(0xffffffbfffffffff));
    assert_eq!(sv39::make_pte(0x81234000, 0xff), 0x2048d0ff);
    assert!(!sv39::is_valid(5));
    assert!(sv39::is_leaf(9));
    assert!(!sv39::is_leaf(1));
    assert_eq!(sv39::vpn_index(0, u32::MAX), 512);
    for flags in 0..1024 {
        compare_sv39(flags, flags);
    }
    let mut state = 0x32588;
    for _ in 0..20000 {
        let address = ((next_random(&mut state) as u64) << 32) | next_random(&mut state) as u64;
        compare_sv39(address, address ^ u64::MAX);
    }
    compare_sv39(u64::MAX, u64::MAX);
}

const PAGE_COUNT: usize = 256;

fn allocate_and_compare(allocator: &mut Pmm<'_>, reference_base: *mut Page) -> Option<usize> {
    // The C allocator was initialized to the live reference pool below.
    let reference = unsafe { pmm_alloc_page() };
    let index = allocator.allocate();
    if reference.is_null() {
        assert_eq!(index, None, "pool exhaustion");
        return None;
    }
    let index = index.expect("Rust allocator exhausted before C");
    assert!(index < PAGE_COUNT);
    assert_eq!(
        index,
        (reference as usize - reference_base as usize) / PAGE_SIZE,
        "sequential allocation and LIFO reuse"
    );
    let page = allocator
        .page_mut(index)
        .expect("allocated page must be accessible");
    assert!(
        page.bytes.iter().all(|&byte| byte == 0),
        "Rust allocation clears the page"
    );
    // Allocation is non-null and its index was checked against the owned pool.
    let reference_bytes =
        unsafe { std::slice::from_raw_parts_mut(reference.cast::<u8>(), PAGE_SIZE) };
    assert!(
        reference_bytes.iter().all(|&byte| byte == 0),
        "C allocation clears the page"
    );
    page.bytes.fill(0xa5);
    reference_bytes.fill(0xa5);
    Some(index)
}

fn release_and_compare(allocator: &mut Pmm<'_>, reference_base: *mut Page, index: usize) {
    assert!(index < PAGE_COUNT);
    // The index identifies a page inside the live reference pool.
    let reference = unsafe { reference_base.add(index).cast::<c_void>() };
    assert_eq!(allocator.release(index), Ok(()));
    assert_eq!(unsafe { pmm_free_page(reference) }, 0);
    assert!(
        allocator.page_mut(index).is_none(),
        "freed page is inaccessible"
    );
    assert_eq!(allocator.release(index), Err(FreeError::DoubleFree));
    assert_eq!(unsafe { pmm_free_page(reference) }, -1);
}

#[test]
fn pmm_exhaustion_reuse_and_20000_c_comparisons() {
    // Vec respects Page's alignment. Allocate backing memory only in the host test.
    let mut reference_pages: Vec<Page> = (0..PAGE_COUNT).map(|_| Page::zeroed()).collect();
    let mut trial_pages: Vec<Page> = (0..PAGE_COUNT).map(|_| Page::zeroed()).collect();
    let reference_base = reference_pages.as_mut_ptr();
    let mut allocator = Pmm::new(&mut trial_pages);
    // Both pointers bound the allocated reference array; it lives until test completion.
    assert_eq!(
        unsafe { pmm_init(reference_base.cast(), reference_base.add(PAGE_COUNT).cast()) },
        0
    );
    assert_eq!(allocator.release(0), Err(FreeError::InvalidPage));
    assert_eq!(unsafe { pmm_free_page(reference_base.cast()) }, -1);
    assert_eq!(allocator.release(usize::MAX), Err(FreeError::InvalidPage));
    assert_eq!(unsafe { pmm_free_page(std::ptr::null_mut()) }, -1);
    let mut live = Vec::new();
    for _ in 0..PAGE_COUNT {
        live.push(allocate_and_compare(&mut allocator, reference_base).unwrap());
    }
    assert_eq!(allocate_and_compare(&mut allocator, reference_base), None);
    while let Some(index) = live.pop() {
        release_and_compare(&mut allocator, reference_base, index);
    }
    let mut state = 0x32588;
    for _ in 0..20000 {
        let value = next_random(&mut state);
        if live.is_empty() || (live.len() < PAGE_COUNT && value & 1 != 0) {
            live.push(allocate_and_compare(&mut allocator, reference_base).unwrap());
        } else {
            let slot = value as usize % live.len();
            release_and_compare(&mut allocator, reference_base, live.swap_remove(slot));
        }
        assert_eq!(
            allocator.available_pages(),
            unsafe { pmm_available_pages() } as usize
        );
        assert_eq!(allocator.available_pages(), PAGE_COUNT - live.len());
    }
    while let Some(index) = live.pop() {
        release_and_compare(&mut allocator, reference_base, index);
    }
    assert_eq!(allocator.available_pages(), PAGE_COUNT);
    let mut empty = Pmm::new(&mut []);
    assert_eq!(empty.allocate(), None);
    assert_eq!(empty.available_pages(), 0);
}

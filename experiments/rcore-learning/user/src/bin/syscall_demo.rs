#![no_std]
#![no_main]

#[used]
#[unsafe(link_section = ".rodata.probe")]
static MESSAGE: [u8; 23] = *b"[paged-syscall] rodata\n";

#[used]
#[unsafe(link_section = ".data.probe")]
static mut INITIALIZED: [u8; 8] = *b"ELF-DATA";

#[used]
#[unsafe(link_section = ".bss.probe")]
static mut ZEROED: [u8; 5000] = [0; 5000];

// Passing an intentionally invalid address as integer syscall arguments is
// allowed; constructing a Rust slice across the unmapped guard would be UB.
fn raw_write(address: usize, length: usize) -> isize {
    let result;
    unsafe {
        core::arch::asm!(
            "ecall", inlateout("a0") 1usize => result,
            in("a1") address, in("a2") length, in("a7") 64usize,
        );
    }
    result
}

#[unsafe(no_mangle)]
fn main() -> i32 {
    if user_lib::write(1, &MESSAGE) != MESSAGE.len() as isize {
        user_lib::exit(1);
    }
    // Fixture-only layout: the final two bytes of the mapped RW page precede
    // the guard at 0x40004000. They are outside BSS, but inside that owned page.
    unsafe {
        (0x4000_3ffe as *mut u8).write_volatile(b'Q');
        (0x4000_3fff as *mut u8).write_volatile(b'Z');
    }
    if raw_write(0x4000_3ffe, 4) != -1 {
        user_lib::exit(2);
    }
    let mut stack_message = *b"[paged-syscall] stack\n";
    // Volatile mutation makes this a real stack buffer rather than a constant
    // rodata alias. The kernel independently checks its VA against stack bounds.
    unsafe { stack_message.as_mut_ptr().write_volatile(b'['); }
    if user_lib::write(1, &stack_message) != stack_message.len() as isize {
        user_lib::exit(3);
    }
    0
}

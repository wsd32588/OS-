#![no_std]
#![no_main]

// Both copies use this exact VA but own different physical data pages. The
// kernel seeds byte zero; a0 is not an entry-to-main parameter convention.
#[used]
#[unsafe(link_section = ".data.probe")]
static mut PRIVATE: [u8; 8] = *b"ELF-DATA";

#[used]
#[unsafe(link_section = ".bss.probe")]
static mut ZEROED: [u8; 5000] = [0; 5000];

fn unknown_syscall() -> isize {
    let result;
    // No pointer arguments are dereferenced by the unknown syscall path.
    unsafe {
        core::arch::asm!("ecall", inlateout("a0") 0usize => result,
            in("a1") 0usize, in("a2") 0usize, in("a7") 0xffffusize);
    }
    result
}

#[unsafe(no_mangle)]
fn main() -> i32 {
    let data = core::ptr::addr_of_mut!(PRIVATE).cast::<u8>();
    let id = unsafe { data.read_volatile() };
    if id > 1 {
        user_lib::exit(1);
    }
    if id == 0 && unknown_syscall() != -1 {
        user_lib::exit(2);
    }
    let rounds = if id == 0 { 2 } else { 3 };
    for round in 0..rounds {
        let value = 10 + id * 10 + round;
        unsafe { data.add(1).write_volatile(value); }
        let message: &[u8] = match (id, round) {
            (0, 0) => b"[paged-task0] round0\n",
            (0, 1) => b"[paged-task0] round1\n",
            (1, 0) => b"[paged-task1] round0\n",
            (1, 1) => b"[paged-task1] round1\n",
            (1, 2) => b"[paged-task1] round2\n",
            _ => user_lib::exit(3),
        };
        if user_lib::write(1, message) != message.len() as isize {
            user_lib::exit(4);
        }
        if user_lib::yield_now() != 0 {
            user_lib::exit(5);
        }
        if unsafe { data.add(1).read_volatile() } != value {
            user_lib::exit(6);
        }
    }
    0
}

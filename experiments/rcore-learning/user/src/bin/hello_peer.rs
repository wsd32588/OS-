#![no_std]
#![no_main]

#[unsafe(no_mangle)]
fn main() -> i32 {
    for round in 0..3 {
        user_lib::println!("[user1] compute round {}", round);
        // Identical finite work lets timer interrupts demonstrate round-robin use.
        unsafe {
            core::arch::asm!(
                "2:",
                "addi {remaining}, {remaining}, -1",
                "bnez {remaining}, 2b",
                remaining = inout(reg) 20_000_000usize => _,
                options(nomem, nostack)
            );
        }
    }
    user_lib::println!("[user1] done");
    0
}

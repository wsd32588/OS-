#![no_std]
#![no_main]

#[unsafe(no_mangle)]
fn main() -> i32 {
    for round in 0..3 {
        user_lib::println!("[user0] compute round {}", round);
        // Finite CPU work, with no syscall or user counter-access assumption.
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
    user_lib::println!("[user0] done");

    return 0;
}

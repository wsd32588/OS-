#![no_std]

use core::panic::PanicInfo;

use crate::syscall::sys_exit;


pub mod console;
mod syscall;

unsafe extern "Rust" {
    fn main() -> i32;
}

unsafe extern "C" {
    static start_bss: u8;
    static end_bss: u8;
}

#[unsafe(no_mangle)]
#[unsafe(link_section = ".text.entry")]
pub extern "C" fn _start() -> ! {
    // BATCH03: kernel supplies sp; clear BSS, call main, pass its code to exit.
    clear_bss();

    let res = unsafe { main() };
    exit(res);
}

fn clear_bss() {
    // BATCH03: use start_bss/end_bss from this application's linker script.
    unsafe{
        let start_bss_addr: isize = core::ptr::addr_of!(start_bss) as isize;
        let end_bss_addr: isize = core::ptr::addr_of!(end_bss) as isize;

        let mut curr_addr: isize = start_bss_addr;

        while curr_addr < end_bss_addr {
            let ptr = curr_addr as *mut u8;

            ptr.write_volatile(0);
            curr_addr += 1;
        }
    };

}

pub fn write(fd: usize, buffer: &[u8]) -> isize {
    syscall::sys_write(fd, buffer)
}

pub fn yield_now() -> isize {
    syscall::sys_yield()
}

pub fn exit(exit_code: i32) -> ! {
    // BATCH03: invoke sys_exit; if it unexpectedly returns, stay in a loop.
    sys_exit(exit_code);
}

#[panic_handler]
fn panic(_information: &PanicInfo<'_>) -> ! {
    // Fixed diagnostic avoids recursive formatting failures in user panic.


    let _ = write(1, b"[user] panic\n");
    exit(1)
}

#![no_std]
#![no_main]

#[repr(C, align(8))]
struct PrivateProgress {
    id: u8,
    stop: u8,
    reserved: [u8; 2],
    counter: u32,
}

#[used]
#[unsafe(link_section = ".data.probe")]
static mut PRIVATE: PrivateProgress = PrivateProgress {
    id: 0, stop: 0, reserved: [0; 2], counter: 1,
};

#[used]
#[unsafe(link_section = ".bss.probe")]
static mut ZEROED: [u8; 5000] = [0; 5000];

#[unsafe(no_mangle)]
fn main() -> i32 {
    let data = core::ptr::addr_of_mut!(PRIVATE);
    let id = unsafe { core::ptr::addr_of!((*data).id).read_volatile() };
    if id > 1 {
        user_lib::exit(1);
    }
    let mut stopped = false;
    // Integer work only, no yield and no user time-CSR access. The driver sets
    // this copy's stop byte after observing actual timer scheduling. The finite
    // bound is a failure escape, not an assumption about a particular tick count.
    for counter in 2u32..500_000_000 {
        unsafe {
            core::ptr::addr_of_mut!((*data).counter).write_volatile(counter);
            if core::ptr::addr_of!((*data).counter).read_volatile() != counter {
                user_lib::exit(2);
            }
            if core::ptr::addr_of!((*data).stop).read_volatile() != 0 {
                stopped = true;
                break;
            }
        }
    }
    if !stopped {
        user_lib::exit(3);
    }
    let message: &[u8] = if id == 0 {
        b"[paged-preempt0] done\n"
    } else {
        b"[paged-preempt1] done\n"
    };
    if user_lib::write(1, message) != message.len() as isize {
        user_lib::exit(4);
    }
    0
}

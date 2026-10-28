#![no_std]
#![no_main]

// These retained objects make the ELF file/memory distinction observable even
// before this image is connected to a running task.
#[used]
#[unsafe(link_section = ".rodata.probe")]
static READ_ONLY: [u8; 17] = *b"ELF-RODATA-DEMO!!";

#[used]
#[unsafe(link_section = ".data.probe")]
static mut INITIALIZED: [u8; 8] = *b"ELF-DATA";

#[used]
#[unsafe(link_section = ".bss.probe")]
static mut ZEROED: [u8; 5000] = [0; 5000];

#[unsafe(no_mangle)]
fn main() -> i32 {
    // Keep user_lib's entry/panic runtime linked. This image is only inspected
    // and initialized in BATCH17; the old hello applications still execute.
    if unsafe { core::ptr::addr_of!(INITIALIZED).cast::<u8>().read_volatile() } != b'E' {
        user_lib::exit(1);
    }
    0
}

pub mod heap_allocator;
pub mod address;
pub mod frame_allocator;
pub mod page_table;
pub mod memory_set;
pub mod paging;
mod framed_demo;
mod elf;
mod elf_demo;
mod user_image;
mod trampoline;
mod paged_user;
mod paged_syscall;
mod paged_task;
mod paged_preempt;

pub fn init() -> Result<(), &'static str> {
    heap_allocator::init_heap()?;
    heap_allocator::run_demo();
    address::run_demo();
    frame_allocator::init()?;
    frame_allocator::run_demo();
    page_table::run_demo();
    memory_set::run_demo()?;
    paging::run_demo()?;
    framed_demo::run_demo()?;
    elf_demo::run_demo()?;
    trampoline::run_demo()?;
    paged_user::run_demo()?;
    paged_syscall::run_demo()?;
    paged_task::run_demo()?;
    paged_preempt::run_demo()?;
    Ok(())
}

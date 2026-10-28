#![no_std]
#![no_main]

extern crate alloc;

use core::arch::global_asm;
use core::panic::PanicInfo;

mod sbi;
mod console;
mod loader;
mod trap;
mod syscall;
mod task;
mod timer;
mod mm;

unsafe extern "C"{ 
    static boot_stack_lower_bound: u8;
    static boot_stack_top: u8;
}

unsafe extern "C" {
    static sbss: u8;
    static ebss: u8;
}


global_asm!(include_str!("entry.asm"));

fn clear_bss() {
    // SAFETY:
    // 链接脚本与当前 QEMU 加载布局保证 [sbss, ebss) 位于可写 RAM；
    // 循环仅在该半开区间内逐字节写零，u8 的对齐要求为 1。
    // 链接脚本先放置 .bss.stack，再定义 sbss，因此不会擦除正在使用的启动栈。
    // 本初始化仅由唯一的启动 hart 执行，且发生在使用 BSS 静态数据之前。
    // write_volatile 保留这些写操作，不提供原子性或并发保护。
    unsafe {
        let sbss_ptr: *const u8 = core::ptr::addr_of!(sbss);
        let ebss_ptr: *const u8 = core::ptr::addr_of!(ebss);

        let sbss_addr: usize = sbss_ptr as usize;
        let ebss_addr: usize = ebss_ptr as usize;

        let mut curr_addr = sbss_addr;
        while ebss_addr > curr_addr {
            let ptr = curr_addr as *mut u8;

            ptr.write_volatile(0);
            curr_addr += 1;
        }

        let lower_bound: *const u8 = core::ptr::addr_of!(boot_stack_lower_bound);
        let top: *const u8 = core::ptr::addr_of!(boot_stack_top);

        let lower_bound_addr: usize = lower_bound as usize;
        let top_addr: usize = top as usize;

        crate::println!("[kernel] stack [{:#x}, {:#x}) bytes={}", lower_bound_addr, top_addr,
            top_addr - lower_bound_addr);
        crate::println!("[kernel] bss [{:#x}, {:#x})", sbss_addr, ebss_addr);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_main() -> ! {
    clear_bss();

    trap::init();
    if let Err(message) = mm::init() {
        crate::println!("[kernel] memory initialization failed: {}", message);
        sbi::shutdown(true);
    }
    // 两个应用都常驻内存；执行顺序由任务管理器决定。
    if let Err(message) = loader::load_apps() {
        crate::println!("[kernel] app loading failed: {}", message);
        sbi::shutdown(true);
    };

    let mut contexts: [
        *mut trap::context::TrapContext;
        loader::APP_COUNT
    ] = [core::ptr::null_mut(); loader::APP_COUNT];

    for app_id in 0..loader::APP_COUNT {
        contexts[app_id] = match loader::prepare_app(app_id) {
            Ok(pointer) => pointer,
            Err(message) => {
                crate::println!("[kernel] app preparation failed: {}", message);
                sbi::shutdown(true);
            }
        };
    

        // SAFETY: 对应栈内现场完整初始化，用户未开始运行，当前独占访问。
        let (entry, user_sp) = unsafe {
            ((*contexts[app_id]).sepc,
            (*contexts[app_id]).x[2])
        };
        crate::println!("kernel app[{}] entry={:#x} bytes={} user_sp={:#x} context={:#x}",
            app_id, entry, loader::APP_IMAGES[app_id].len(), user_sp, contexts[app_id] as usize
        );
    }

    // SAFETY: 每个现场都在上面初始化过；还没有用户任务在跑，
    // 且 trap::init 已在这唯一的启动 hart 上关闭了 S 中断。
    if let Err(message) = unsafe { task::init(contexts) } {
        crate::println!("[kernel] task initialization failed: {}", message);
        sbi::shutdown(true);
    }

    // trap::init 清空了 sie；必须等现场和任务都就绪后再武装定时器。
    if let Err(message) = timer::init() {
        crate::println!("[kernel] timer initialization failed: {}", message);
        sbi::shutdown(true);
    }
    crate::println!("[kernel] timer interval={} ticks", timer::TIMER_INTERVAL);

    task::run_first()
 
}

#[panic_handler]
fn panic(_information: &PanicInfo<'_>) -> ! {
    if let Some(location) = _information.location() {
        crate::println!(
            "[panic] {}:{} {}",
            location.file(),
            location.line(),
            _information.message()
        );
    } else {
        crate::println!("[panic] {}", _information.message());
    }
    // panic 表示本次运行失败，不是正常结束路径。
    sbi::shutdown(true)
}

use crate::trap::context::TrapContext;

pub const APP_BASE_ADDRESS: usize = 0x80400000;
pub const APP_SIZE_LIMIT: usize = 0x20000;
pub const STACK_SIZE: usize = 4096 * 2;
pub const APP_COUNT: usize = 2;
pub const START_APP_ID: usize = 0;

pub static APP_IMAGES: [&[u8]; APP_COUNT] = [
    include_bytes!("../../user/target/riscv64gc-unknown-none-elf/release/hello.bin"),
    include_bytes!("../../user/target/riscv64gc-unknown-none-elf/release/hello_peer.bin"),
];

const _: () = assert!(START_APP_ID < APP_COUNT);

#[repr(C, align(4096))]
struct Stack {
    bytes: [u8; STACK_SIZE],
}

// 由内核/恢复代码直接写入；不要对这些 static 创建引用。
static mut USER_STACK: [Stack; APP_COUNT] = [const { Stack { bytes: [0; STACK_SIZE] } }; APP_COUNT];
static mut KERNEL_STACK: [Stack; APP_COUNT] = [const { Stack { bytes: [0; STACK_SIZE] } }; APP_COUNT];
const _: () = assert!(core::mem::size_of::<Stack>() == STACK_SIZE);

fn validate_app_id(app_id: usize) -> Result<(), &'static str> {
    if app_id >= APP_COUNT {
        return Err("invalid application id");
    }
    Ok(())
}

pub fn app_base_address(app_id: usize) -> Result<usize, &'static str> {
    // BATCH06：先校验 id，再计算 APP_BASE_ADDRESS + id * APP_SIZE_LIMIT。
    validate_app_id(app_id)?;
    
    Ok(APP_BASE_ADDRESS + app_id * APP_SIZE_LIMIT)
}

pub fn load_apps() -> Result<(), &'static str> {
    // BATCH06：枚举 APP_IMAGES，把每个镜像装进各自的槽位。
    for (app_id, image) in APP_IMAGES.iter().enumerate() {
        load_image(app_id, image)?;
    }
    
    Ok(())
}

fn read_sstatus() -> usize {
    let value: usize;
    // SAFETY: 启动阶段运行在 S 模式，允许读取 sstatus。
    unsafe {
        core::arch::asm!("csrr {}, sstatus", out(reg) value, options(nomem, nostack));
    }
    value
}

pub fn load_image(app_id: usize, image: &[u8]) -> Result<usize, &'static str> {
    // BATCH04：写入前先拒绝非法长度和源/目标重叠；
    // 清零目标窗口、复制字节，再执行 fence.i。只在启动阶段、用户执行之前调用。
    let target_start = app_base_address(app_id)?;

    if image.is_empty() ||
        image.len() > APP_SIZE_LIMIT
    {
        return Err("NULL image or out of app size limit");
    } 

    let source_start = image.as_ptr() as usize;
    let source_end = match source_start.checked_add(image.len()) {
        Some(end) => end,
        None => return Err("source address overflow"),
    };
    let target_end = match target_start.checked_add(APP_SIZE_LIMIT) {
        Some(end) => end,
        None => return Err("target address overflow"),
    };
    if source_start < target_end && target_start < source_end {
        return Err("source and destination overlap");
    }
    // SAFETY: 目标窗口位于当前 QEMU 的可写物理 RAM；链接断言保证内核不占用此窗口。
    // 尺寸与源/目标不重叠已检查，写入偏移有界；单 hart 初始化时没有目标内存的其它访问者。
    // fence.i 保证本 hart 后续取指能看到先前写入的机器码，不指定硬件必须如何刷新缓存。
    unsafe {
        let target_ptr = target_start as *mut u8;
        for i in 0..APP_SIZE_LIMIT {
            target_ptr.add(i).write_volatile(0);
        }

        for (i, &byte) in image.iter().enumerate() {
            target_ptr.add(i).write_volatile(byte);
        }

        core::arch::asm!("fence.i");
    };

    Ok(image.len())
}

pub fn prepare_app(app_id: usize) -> Result<*mut TrapContext, &'static str> {
    // 两个镜像必须已经装载完成；这里只初始化保存的现场。
    let entry = app_base_address(app_id)?;
    let (_, user_stack_top) = user_stack_bounds(app_id)?;
    let (_, kernel_stack_top) = kernel_stack_bounds(app_id)?;

    let cx = TrapContext::app_init_context(entry, user_stack_top, read_sstatus());
    
    let cx_addr = kernel_stack_top - core::mem::size_of::<TrapContext>();
    let cx_ptr = cx_addr as *mut TrapContext;
    
    // SAFETY: 当前应用的两栈尚未使用；272 字节现场完全位于可写内核栈顶部且满足对齐。
    // 当前单 hart 独占初始化，cx 已初始化全部字段。
    unsafe {
        cx_ptr.write_volatile(cx);
    }
    
    Ok(cx_ptr)
}

// 系统调用层也接受位于用户静态栈内的缓冲区。
pub fn user_stack_bounds(app_id: usize) -> Result<(usize, usize), &'static str> {
    validate_app_id(app_id)?;
    let bottom = core::ptr::addr_of_mut!(USER_STACK) as usize + app_id * STACK_SIZE;
    Ok((bottom, bottom + STACK_SIZE))
}

pub fn kernel_stack_bounds(app_id: usize) -> Result<(usize, usize), &'static str> {
    validate_app_id(app_id)?;
    let bottom = core::ptr::addr_of_mut!(KERNEL_STACK) as usize + app_id * STACK_SIZE;
    Ok((bottom, bottom + STACK_SIZE))
}

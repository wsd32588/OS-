
const SYSCALL_WRITE: usize = 64;
const SYSCALL_EXIT: usize = 93;
const SYSCALL_YIELD: usize = 124;
const FD_STDOUT: usize = 1;

pub fn syscall(id: usize, arguments: [usize; 3]) -> isize {
    // BATCH05：分发 write/exit；未知调用号返回 -1。
    match id {
        SYSCALL_WRITE => {
            sys_write(arguments[0], arguments[1], arguments[2])
        }
        SYSCALL_EXIT => sys_exit(arguments[0] as i32),
        SYSCALL_YIELD => {
            crate::task::suspend_current_and_run_next();
            0
        }
        _ => -1,
    }
}

fn user_buffer_is_readable(start: usize, length: usize) -> bool {
    // BATCH05：用 checked_add 求末尾；整个缓冲区必须完整落在应用窗口
    // 或 loader::user_stack_bounds() 返回的用户栈范围内。

    let end = match start.checked_add(length) {
        Some(end) => end,
        None => return false,
    };
    let app_id = crate::task::current_task_id();
    let app_start = match crate::loader::app_base_address(app_id) {
        Ok(address) => address,
        Err(_) => return false,
    };
    let app_end = app_start + crate::loader::APP_SIZE_LIMIT;
    let (stack_start, stack_end) = match crate::loader::user_stack_bounds(app_id) {
        Ok(bounds) => bounds,
        Err(_) => return false,
    };

    return app_start <= start && end <= app_end || stack_start <= start && end <= stack_end
}

fn sys_write(fd: usize, buffer_addr: usize, length: usize) -> isize {
    // BATCH05：只支持 fd=1；空写直接成功；读取前先拒绝非法范围。
    // 通过裸指针读字节，并逐字节交给 sbi::console_putchar。
    if fd != FD_STDOUT {
        return -1;
    }

    if length == 0 {
        return 0;
    }

    let start = buffer_addr;

    if !user_buffer_is_readable(start, length) {
        return -1;
    }

    for i in 0..length {
        let pointer = (start + i) as *const u8;

        // SAFETY: 前面已检查完整地址范围、加法不溢出；
        // 用户暂停执行，这些字节已初始化且可读取。
        let byte = unsafe { pointer.read_volatile() };

        crate::sbi::console_putchar(byte);
    }

    length as isize
}

fn sys_exit(exit_code: i32) -> ! {
    crate::task::exit_current_and_run_next(exit_code)
}

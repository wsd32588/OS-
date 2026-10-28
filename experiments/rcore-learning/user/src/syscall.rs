//use core::arch::asm;

const SYSCALL_WRITE: usize = 64;
const SYSCALL_EXIT: usize = 93;
const SYSCALL_YIELD: usize = 124;

fn syscall(id: usize, arguments: [usize; 3]) -> isize {
    // BATCH03: ecall; a7=id, a0..a2=arguments, a0 returns an isize.
    let result: isize;
    // SAFETY: This wrapper assumes the kernel consumes write buffers synchronously
    // and restores user registers except a0, which carries the syscall result.
    unsafe {
        //SAFTY:本包装依赖内核遵守 syscall 接口
        //并保存恢复除返回值 a0 外的用户寄存器
        core::arch::asm!(
            "ecall",
            inlateout("a0") arguments[0] => result,
            in("a1") arguments[1],
            in("a2") arguments[2],
            in("a7") id,
        );
    }
    result
}

pub fn sys_write(fd: usize, buffer: &[u8]) -> isize {
    // BATCH03: derive pointer and length from the borrowed slice.
    syscall(SYSCALL_WRITE, [fd, buffer.as_ptr() as usize, buffer.len()])
}

pub fn sys_exit(exit_code: i32) -> ! {
    syscall(SYSCALL_EXIT, [exit_code as usize, 0 as usize, 0 as usize]);
    loop {}
}

pub fn sys_yield() -> isize {
    syscall(SYSCALL_YIELD, [0, 0, 0])
}

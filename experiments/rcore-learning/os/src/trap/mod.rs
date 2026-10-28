pub mod context;

use core::arch::{ global_asm };
use context::TrapContext;

use crate::sbi;

const INTERRUPT_FLAG: usize = 1usize << (usize::BITS - 1);
const SUPERVISOR_TIMER_INTERRUPT: usize = INTERRUPT_FLAG | 5;

global_asm!(include_str!("trap.S"));

unsafe extern "C" {
    fn __alltraps();
}

pub fn init() {
    // 启动阶段先关闭 S 中断；稍后由 timer::init 只打开 sie.STIE。
    let entry = __alltraps as *const() as usize;
    unsafe {
        core::arch::asm!(
            "csrci sstatus, 2",
            "csrw sie, zero",
            "csrw stvec, {entry}",
            entry = in(reg) entry,
            options(nostack)
        );
    };
}

/// # Safety
/// 只允许 __alltraps 用它拥有独占访问权、已对齐、已完整保存的
/// TrapContext 来调用。这个入口目前只支持来自 U 模式的陷入。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn trap_handler(context_pointer: *mut TrapContext) -> *mut TrapContext {
    // U 模式 ecall 推进 sepc 并写 a0；timer 中断则原样保留整个用户现场。
    // 两条路径在挂起的调用恢复后，都返回原来的现场指针。

    let scause: usize;
    let stval: usize;
    
    unsafe {
        core::arch::asm!(
            "csrr {scause}, scause",
            "csrr {stval}, stval",
            scause = out(reg) scause,
            stval = out(reg) stval,
            options(nomem, nostack)
        );
    }
    
         match scause {
            8 => {
                // 先拷出系统调用参数，然后在可能挂起当前内核调用链的 yield
                // 之前结束对现场的借用。
                let (id, argument) = {
                    let ctx = unsafe { &mut *context_pointer };
                    ctx.sepc += 4;
                    (ctx.x[17], [ctx.x[10], ctx.x[11], ctx.x[12]])
                };
                let result = crate::syscall::syscall(id, argument);
                // SAFETY: 系统调用返回说明又是这个任务在跑；它的内核栈
                // 和保存的现场在挂起期间一直存活。
                unsafe { (*context_pointer).x[10] = result as usize; }
            }

            SUPERVISOR_TIMER_INTERRUPT => {
                // BATCH09 D：重新武装下一次定时器（返回 Err 时先打印诊断，
                // 再关机），然后抢占当前任务。
                // 这条中断路径上不要推进 sepc，也不要改保存的 a0。
                match crate::timer::set_next_trigger() {
                    Ok(()) => crate::task::preempt_current_and_run_next(),
                    Err(str) => {
                        crate::println!("set next trigger failed, {}",str);
                        sbi::shutdown(true);   
                    }
                };
                    
            }

            _ => {
                crate::println!(
                    "[kernel] unexpected trap: scause={:#x} sepc={:#x} stval={:#x}",
                    scause,
                    unsafe { (*context_pointer).sepc },
                    stval
                );
                crate::sbi::shutdown(true);
            }
        };
    context_pointer
    
}

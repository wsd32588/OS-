pub mod context;
pub mod manager;

use core::arch::global_asm;
use core::ptr::{addr_of, addr_of_mut};

use crate::loader::APP_COUNT;
use crate::trap::context::TrapContext;
use context::TaskContext;
use manager::{TaskControlBlock, TaskManager, TaskStatus};

global_asm!(include_str!("switch.S"));

unsafe extern "C" {
    fn __switch(current: *mut TaskContext, next: *const TaskContext);
}

// 单 hart，S 模式执行期间保持 SIE=0。不要跨 __switch 保留引用：
// 我们暂停期间，别的任务要能访问同一个对象。
static mut TASK_MANAGER: TaskManager = TaskManager::empty();

/// # Safety
/// 只在启动阶段调用一次，且 loader 已经为每个应用初始化好各自唯一的、
/// 仍然存活的现场。此时没有任务在跑，S 中断保持关闭。
pub unsafe fn init(contexts: [*mut TrapContext; APP_COUNT]) -> Result<(), &'static str> {
    let manager = addr_of_mut!(TASK_MANAGER);
    // SAFETY: 启动阶段独占管理器；这次读取后不会留下引用。
    if unsafe { (*manager).initialized } {
        return Err("task manager already initialized");
    }
    // 修改全局任务表之前，先检查所有输入指针。
    for (app_id, context) in contexts.iter().enumerate() {
        let (_, top) = crate::loader::kernel_stack_bounds(app_id)?;
        if *context as usize != top - core::mem::size_of::<TrapContext>() {
            return Err("unexpected trap context address");
        }
    }
    let tasks = core::array::from_fn(|app_id| TaskControlBlock {
        status: TaskStatus::Ready,
        task_context: TaskContext::goto_restore(contexts[app_id] as usize),
    });
    // SAFETY: 所有现场都已校验；这是唯一一次初始化写入。
    unsafe {
        manager.write(TaskManager {
            tasks,
            current: crate::loader::START_APP_ID,
            initialized: true,
            had_failure: false,
        });
    }
    Ok(())
}

pub fn current_task_id() -> usize {
    // SAFETY: 只有正在运行的启动/任务路径会执行到这里；借用在此结束。
    unsafe { (&*addr_of!(TASK_MANAGER)).current_task_id() }
}

pub fn run_first() -> ! {
    let next = {
        // SAFETY: 已由 rust_main 初始化，且在用户代码执行前只调用一次。
        let manager = unsafe { &mut *addr_of_mut!(TASK_MANAGER) };
        manager.start_first()
    };
    crate::println!("[task] start {}", next);
    let mut boot_context = TaskContext::zero_init();
    // SAFETY: 没有任何管理器借用跨过 __switch。boot 记录保持存活；
    // 下一个任务的启动上下文指向它已准备好、已对齐的 TrapContext。
    unsafe {
        let next_context = addr_of!((*addr_of!(TASK_MANAGER)).tasks[next].task_context);
        __switch(addr_of_mut!(boot_context), next_context);
    }
    panic!("first task unexpectedly returned to boot");
}

pub fn suspend_current_and_run_next() {
    suspend_and_run_next("yield");
}

pub fn preempt_current_and_run_next() {
    suspend_and_run_next("timer");
}

fn suspend_and_run_next(reason: &'static str) {
    let (current, next) = {
        // SAFETY: 这段短借用会在任何任务走其他路径之前结束。
        let manager = unsafe { &mut *addr_of_mut!(TASK_MANAGER) };
        manager.suspend_current()
    };
    crate::println!("[task] {} {} -> {}", reason, current, next);
    if current != next {
        // SAFETY: 状态转换已经选中一个有效且不同的 Ready 任务，并把它
        // 置为 Running。两个任务记录和内核栈都保持存活。
        unsafe { switch_task(current, next); }
    }
    // 能回到这里，说明又是同一个任务被选中。若只有它一个可运行，
    // 也会直接返回这里，不会让 __switch 的两份记录产生别名。
}

pub fn exit_current_and_run_next(exit_code: i32) -> ! {
    let (current, next, had_failure) = {
        // SAFETY: 别的任务执行期间，这里不保留任何引用或守卫。
        let manager = unsafe { &mut *addr_of_mut!(TASK_MANAGER) };
        let current = manager.current_task_id();
        let next = manager.exit_current(exit_code);
        (current, next, manager.had_failure)
    };
    crate::println!("[kernel] application {} exited: code={}", current, exit_code);
    match next {
        Some((previous, next)) => {
            assert_eq!(previous, current);
            assert_ne!(current, next);
            crate::println!("[task] exit {} -> {}", current, next);
            // SAFETY: 已退出任务的栈仍然分配着，但永远不会
            // 再被选中。下一个任务已经初始化并处于 Running。
            unsafe { switch_task(current, next); }
            panic!("exited task was resumed");
        }
        None => {
            crate::println!("[kernel] all applications exited");
            crate::sbi::shutdown(had_failure);
        }
    }
}

/// # Safety
/// 传入有效且不同的 id；状态已更新；栈仍然存活；没有管理器引用
/// 或资源守卫跨过切换；下一个任务已被置为 Running。
unsafe fn switch_task(current: usize, next: usize) {
    assert!(current < APP_COUNT && next < APP_COUNT && current != next);
    let manager = addr_of_mut!(TASK_MANAGER);
    // SAFETY: 用裸字段指针，保证即使本函数还有另一个挂起的调用活在别的栈上，
    // 也能独立访问各自的任务记录。
    unsafe {
        let current_context = addr_of_mut!((*manager).tasks[current].task_context);
        let next_context = addr_of!((*manager).tasks[next].task_context);
        __switch(current_context, next_context);
    }
}

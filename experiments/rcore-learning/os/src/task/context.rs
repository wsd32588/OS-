// 内核调用续点；偏移必须与 RV64 上的 switch.S 保持一致。
#[repr(C)]
pub struct TaskContext {
    pub ra: usize,
    pub sp: usize,
    pub s: [usize; 12],
}

const _: () = assert!(core::mem::size_of::<TaskContext>() == 112);
const _: () = assert!(core::mem::offset_of!(TaskContext, ra) == 0);
const _: () = assert!(core::mem::offset_of!(TaskContext, sp) == 8);
const _: () = assert!(core::mem::offset_of!(TaskContext, s) == 16);

unsafe extern "C" {
    fn __task_bootstrap();
}

impl TaskContext {
    pub fn zero_init() -> Self {
        // BATCH07：所有字段初始化为 0；它只是保存现场用的存储，
        // 不是第一次保存之前就能恢复执行的上下文。
        TaskContext { ra: (0), sp: (0), s: ([0; 12]) }    
    }

    pub fn goto_restore(trap_context_address: usize) -> Self {
        // BATCH07：ra=__task_bootstrap，sp=TrapContext 地址，s 寄存器全 0。
        // __task_bootstrap 把那个栈指针适配到现有的 __restore ABI。
        let tcx: TaskContext = TaskContext{
            ra: __task_bootstrap as *const () as usize,
            sp: trap_context_address,
            s: [0; 12]
        };
        tcx
    }
}

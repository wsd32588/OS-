pub const SSTATUS_SPP: usize = 1 << 8;
pub const SSTATUS_SIE: usize = 1 << 1;
pub const SSTATUS_SPIE: usize = 1 << 5;

// 恢复汇编使用这份精确的 RV64 整数现场布局。
#[repr(C)]
pub struct TrapContext {
    pub x: [usize; 32],
    pub sstatus: usize,
    pub sepc: usize,
}

const _: () = assert!(core::mem::size_of::<TrapContext>() == 34 * 8);

impl TrapContext {
    pub fn app_init_context(entry: usize, user_stack_top: usize, current_sstatus: usize) -> Self {
        // 初始化用户寄存器；sret 之前保持 SIE=0，并通过 SPP 选择返回 U 模式。
        let mut x = [0usize; 32];
        x[2] = user_stack_top;

        let tcxt= TrapContext {
            x,
            // 恢复现场在 sret 之前仍运行在 S 模式，所以 SIE 必须保持 0。
            sstatus: (current_sstatus & !(SSTATUS_SPP | SSTATUS_SIE)) | SSTATUS_SPIE,
            sepc: entry
        };
        
        return tcxt;
    }
}

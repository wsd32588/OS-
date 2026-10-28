// QEMU virt 的时基，已从其 DTB 核对过；它不是 CPU 主频。
pub const CLOCK_FREQUENCY: u64 = 10_000_000;
pub const TICKS_PER_SECOND: u64 = 100;
pub const TIMER_INTERVAL: u64 = CLOCK_FREQUENCY / TICKS_PER_SECOND;
const SIE_STIE: usize = 1 << 5;


pub fn read_time() -> u64 {
    // BATCH09 A：用 csrr 读 time CSR，返回它的 RV64 值。
    let res: u64;
    unsafe {
        core::arch::asm!(
            "csrr {value}, time",
            value = out(reg) res,
            options(nomem, nostack)
        );
    }
    res
}

pub fn set_next_trigger() -> Result<(), &'static str> {
    // BATCH09 B：用 checked_add 计算 read_time() + TIMER_INTERVAL；
    // 溢出则拒绝，否则把绝对截止时间交给 sbi::set_timer。
    let now = read_time();
    let deadline = now
        .checked_add(TIMER_INTERVAL)
        .ok_or("timer deadline overflow")?;
    crate::sbi::set_timer(deadline)
}

fn enable_timer_interrupt() {
    // BATCH09 C：用 csrs 和承载 SIE_STIE 的输入寄存器置起 sie.STIE。
    // 保持运行中的 sstatus.SIE 为 0：trap 入口目前只支持来自 U 模式的陷入。
    unsafe {
        core::arch::asm!(
            "csrs sie, {mask}",
            mask = in(reg)SIE_STIE
        )
    }

}

pub fn init() -> Result<(), &'static str> {
    if !crate::sbi::timer_available() {
        return Err("SBI TIME extension is unavailable");
    }
    set_next_trigger()?;
    enable_timer_interrupt();
    Ok(())
}

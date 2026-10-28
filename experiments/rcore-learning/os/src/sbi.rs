pub fn console_putchar(byte: u8) {
    // BATCH01 C：用 sbi_rt::legacy::console_putchar，并显式做类型转换。
    // 在局部屏蔽它的弃用警告：这是为了配合第一章的固件接口。
    #[allow(deprecated)]
    sbi_rt::legacy::console_putchar(usize::from(byte));

}

pub fn timer_available() -> bool {
    sbi_rt::probe_extension(sbi_rt::Timer).is_available()
}

pub fn set_timer(deadline: u64) -> Result<(), &'static str> {
    let result = sbi_rt::set_timer(deadline);
    if result.is_err() {
        return Err("SBI TIME set_timer failed");
    }
    Ok(())
}

pub fn shutdown(failure: bool) -> ! {
    // BATCH01 C：调用 sbi_rt::system_reset(Shutdown, NoReason/SystemFailure)。
    // 如果固件意外返回，就停在这里，不要回到 Rust 代码。
    if failure {
        let _ = sbi_rt::system_reset(
            sbi_rt::Shutdown,
            sbi_rt::SystemFailure
        );
    } else {
        let _ = sbi_rt::system_reset(
            sbi_rt::Shutdown,
            sbi_rt::NoReason
        );
    };
    
    // SAFETY: 如果硬件复位失败，此循环将无限挂起 CPU，确保函数永远不会返回
    loop{}
}


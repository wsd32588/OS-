use core::fmt::{self, Write};
struct Console;

impl Write for Console {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        for byte in text.bytes() {
            crate::sbi::console_putchar(byte);
        }
        Ok(())
    }
}

pub fn print(arguments: fmt::Arguments<'_>) {
    let mut console = Console;
    if console.write_fmt(arguments).is_err() {
        for byte in "[console] format failed\n".bytes() {
            crate::sbi::console_putchar(byte);
        }
        crate::sbi::shutdown(true);
    }
}

#[macro_export]
macro_rules! print {
    ($($argument:tt)*) => {{
        $crate::console::print(core::format_args!
        ($($argument)*));
    }};
}

#[macro_export]
macro_rules! println {
    () => {{
        $crate::print!("\n");
    }};
    ($($argument:tt)+) => {{
        $crate::print!("{}\n",
    core::format_args!($($argument)+));
    }};
}
use core::fmt::{self, Write};

struct Console;

impl Write for Console {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        // BATCH03: one user write call; require the entire slice to be written.
        let result: isize = crate::write(1, text.as_bytes());
        if result < 0 ||
            result as usize != text.len() {
                return Err(core::fmt::Error)
        }

        return Ok(())
    }
}

pub fn print(arguments: fmt::Arguments<'_>) {
    let mut console = Console;
    if console.write_fmt(arguments).is_err() {
        // Bypass formatting on this failure path; the fixed message is best effort.
        let _ = crate::write(1, b"[user] format failed\n");
        crate::exit(1);
    }
}

#[macro_export]
macro_rules! print {
    ($($argument:tt)*) => {{
        $crate::console::print(core::format_args!($($argument)*));
    }};
}

#[macro_export]
macro_rules! println {
    () => {{ $crate::print!("\n"); }};
    ($($argument:tt)+) => {{
        $crate::print!("{}\n", core::format_args!($($argument)+));
    }};
}

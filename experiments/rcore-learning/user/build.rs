fn main() {
    // Linker-script changes must invalidate the cached user binaries.
    println!("cargo:rerun-if-changed=src/linker.ld");
    println!("cargo:rerun-if-changed=src/paged.ld");
}

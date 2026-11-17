void open_file_test(const tinyos::fs::BuiltinFile& file) {
    tinyos::fs::OpenFilePool pool {};
    tinyos::fs::FileDescriptorTable parent {};
    tinyos::fs::FileDescriptorTable child {};

    const int first_fd = parent.open(pool, file);
    const int second_fd = parent.open(pool, file);
    if (first_fd != 0 || second_fd != 1) {
        PANIC_KERNEL("opening about twice failed");
    }

    auto* first = parent.get(first_fd);
    auto* second = parent.get(second_fd);
    if (first == nullptr || second == nullptr || first == second) {
        PANIC_KERNEL("independent open objects invalid");
    }

    unsigned char buffer[18]{};
    if (first->read_current(std::span<unsigned char>(buffer, 7)) != 7 ||
        first->commit_read(7) != 0 ||
        first->offset() != 7 || second->offset() != 0) {
        PANIC_KERNEL("independent positions invalid");
    }

    if (child.inherit_from(pool, parent) != 0 ||
        child.get(0) != first || child.get(1) != second ||
        first->reference_count() != 2 || second->reference_count() != 2 ||
        pool.in_use() != 2) {
        PANIC_KERNEL("fd inheritance invalid");
    }

    if (child.get(0)->read_current(
            std::span<unsigned char>(buffer + 7, 11)) != 11 ||
        child.get(0)->commit_read(11) != 0 ||
        child.get(0)->offset() != 18 || first->offset() != 18 ||
        second->offset() != 0) {
        PANIC_KERNEL("shared offset invalid");
    }

    if (parent.close(pool, 0) != 0 ||
        parent.get(0) != nullptr ||
        child.get(0) == nullptr ||
        child.get(0)->reference_count() != 1) {
        PANIC_KERNEL("close one reference invalid");
    }

    parent.close_all(pool);
    child.close_all(pool);
    if (pool.in_use() != 0 || !parent.empty() || !child.empty()) {
        PANIC_KERNEL("fd model cleanup invalid");
    }
    parent.close_all(pool);
    child.close_all(pool);

    uart_puts("[fs] FD model OK\n");
}

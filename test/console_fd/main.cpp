#include "fs/file_syscall.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "console/console_service.hpp"
#include "fs/builtin_fs.hpp"
#include "fs/file_descriptor_table.hpp"
#include "fs/file_service.hpp"
#include "fs/open_file.hpp"
#include "fs/standard_stream.h"
#include "process/process.hpp"
#include "runtime/panic.h"
#include "task/syscall.h"
#include "user_memory_fixture.hpp"

extern "C" [[noreturn]] void kernel_panic(const char* file, int line, const char* message) {
    std::fprintf(stderr, "[HOST PANIC] %s:%d: %s\n", file, line, message);
    std::abort();
}

namespace {
using namespace tinyos_test;
namespace console = tinyos::console;
namespace fs = tinyos::fs;

constexpr char kConsolePath[] = "/dev/console";
constexpr char kAboutPath[] = "/etc/about";
constexpr char kEmptyPath[] = "/empty";
constexpr unsigned char kAbout[] = "TinyOS builtin fs\n";
fs::BuiltinFileSystem g_filesystem;
bool g_user_mode{};

struct Capture {
    std::array<unsigned char, 512> input{};
    std::array<unsigned char, 4096> output{};
    size_t input_size{};
    size_t input_position{};
    size_t reads{};
    size_t output_size{};
    size_t writes{};
    bool overflow{};

    static int read(void* context) noexcept {
        auto& capture = *static_cast<Capture*>(context);
        ++capture.reads;
        return capture.input_position == capture.input_size
            ? -1 : capture.input[capture.input_position++];
    }

    static void write(void* context, std::span<const unsigned char> bytes) noexcept {
        auto& capture = *static_cast<Capture*>(context);
        ++capture.writes;
        if (bytes.size() > capture.output.size() - capture.output_size) {
            capture.overflow = true;
            return;
        }
        for (unsigned char byte : bytes) {
            capture.output[capture.output_size++] = byte;
        }
    }

    console::ConsoleIo io() noexcept { return {this, read, write}; }
};

// Both services borrow permanent providers; each CTest process binds them once.
Capture g_capture;

void supply(std::span<const unsigned char> bytes) {
    unsigned char pending{};
    require_equal(console::read_console_current({&pending, 1}), 0L,
                  "supply new fake hardware input only after the prior transaction drained");
    require(bytes.size() <= g_capture.input.size(), "the finite input source has sufficient storage");
    std::copy(bytes.begin(), bytes.end(), g_capture.input.begin());
    g_capture.input_size = bytes.size();
    g_capture.input_position = 0;
}

struct PageSnapshot {
    unsigned long allocations{host_allocation_calls()};
    CallCounts calls{host_call_counts()};
    unsigned long available{pmm_available_pages()};

    void expect_unchanged() const {
        require_equal(host_allocation_calls(), allocations, "FD operations allocate no physical pages");
        require_equal(host_call_counts(), calls, "FD operations do not unmap, flush or free pages");
        require_equal(pmm_available_pages(), available, "FD operations preserve available physical pages");
    }
};

class ProcessFixture {
public:
    static constexpr uintptr_t kSource = 0x40000000;
    static constexpr uintptr_t kDestination = kSource + SV39_PAGE_SIZE;
    tinyos::Process process{};
    unsigned char* source{};
    unsigned char* destination{};

    ProcessFixture() {
        host_set_satp(0);
        kernel_ = sv39_create_page_table();
        require(kernel_ != nullptr, "allocate a borrowed kernel template root");
        for (size_t i = 0; i < shared_.size(); ++i) {
            shared_[i] = sv39_create_page_table();
            require(shared_[i] != nullptr, "allocate a borrowed shared template branch");
            kernel_->entries[i * 2] = sv39_make_pte(
                reinterpret_cast<uintptr_t>(shared_[i]), SV39_PTE_V);
        }
        UserAddressSpace candidate{};
        require_equal(user_address_space_create(kernel_, &candidate), SV39_OK, "create an owned private root");
        candidate.user_memory.root = candidate.root;
        for (size_t i = 0; i < 2; ++i) {
            void* page = pmm_alloc_page();
            require(page != nullptr, "allocate a genuinely owned user page");
            std::memset(page, 0xcc, SV39_PAGE_SIZE);
            const uintptr_t address = kSource + i * SV39_PAGE_SIZE;
            candidate.user_memory.pages[i] = {address, page};
            ++candidate.user_memory.page_count;
            const uint64_t flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_A |
                (i == 0 ? 0 : SV39_PTE_W | SV39_PTE_D);
            require_equal(sv39_map_page(candidate.root, address, reinterpret_cast<uintptr_t>(page), flags),
                          SV39_OK, "map a real user-readable source or writable destination");
            if (i == 0) {
                source = static_cast<unsigned char*>(page);
            } else {
                destination = static_cast<unsigned char*>(page);
            }
        }
        process.adopt(candidate);
    }

    long open(std::string_view path) {
        std::memcpy(source, path.data(), path.size());
        return fs::syscall_open_file(process, kSource, path.size());
    }

    void finish() {
        process.close_files();
        require_equal(process.release_address_space(), SV39_OK, "release all owned fixture mappings");
        for (auto* shared : shared_) {
            require_equal(pmm_free_page(shared), 0, "release a borrowed template branch");
        }
        require_equal(pmm_free_page(kernel_), 0, "release the borrowed template root");
        require_equal(pmm_available_pages(), 16UL, "all fixture pages are restored");
    }

private:
    PagePool<16> pages_;
    Sv39PageTable* kernel_{};
    std::array<Sv39PageTable*, 2> shared_{};
};

void test_one_time_mode_configuration() {
    PagePool<1> pages;
    const fs::BuiltinFile files[] = {
        {{kAboutPath, sizeof(kAboutPath) - 1}, {kAbout, sizeof(kAbout) - 1}},
        {{kEmptyPath, sizeof(kEmptyPath) - 1}, {}}
    };
    require_equal(g_filesystem.init(files), 0, "initialize the permanent filesystem");
    require_equal(fs::configure_file_service(g_filesystem), 0, "bind the filesystem once");
    fs::FileDescriptorTable table{};
    fs::OpenFilePool pool{};
    require_equal(fs::open_file(table, {kConsolePath, sizeof(kConsolePath) - 1}), -1,
                  "a device path cannot open before the device is ready");
    require(pool.acquire_console() == nullptr && table.empty(), "unready acquire changes no FD state");
    require_equal(fs::prepare_standard_streams(table), -1,
                  "standard streams cannot prepare before the console is ready");
    require(table.empty(), "unready standard-stream preparation leaves the target empty");
    const auto mode = g_user_mode ? console::ConsoleInputMode::UserProcesses
                                  : console::ConsoleInputMode::KernelDebugger;
    require_equal(console::configure_console(g_capture.io(), static_cast<console::ConsoleInputMode>(99)), -1,
                  "an unknown input mode is rejected before publishing callbacks");
    require_equal(console::configure_console({}, mode), -1, "invalid callbacks cannot publish the requested mode");
    require(!console::console_ready() && !console::console_user_io_enabled(), "failed configuration leaves the service unready");
    require_equal(console::configure_console(g_capture.io(), mode), 0, "bind this process's selected mode once");
    const std::array<unsigned char, 1> input{'Z'};
    supply(input);
    require_equal(console::poll_console_input(), 1L, "queue a byte before rejected configuration");
    const auto other = g_user_mode ? console::ConsoleInputMode::KernelDebugger
                                   : console::ConsoleInputMode::UserProcesses;
    require_equal(console::configure_console(g_capture.io(), other), -1, "a second configuration cannot switch input ownership");
    require_equal(console::console_user_io_enabled(), g_user_mode, "rejected binding preserves the original mode");
    unsigned char byte{};
    require_equal(console::read_console_current({&byte, 1}), 1L, "rejected binding preserves pending input");
    require_equal(byte, static_cast<unsigned char>('Z'), "the original queued byte survives configuration rejection");
    require_equal(console::commit_console_read(1), 0, "drain the configuration test byte normally");
    if (!g_user_mode) {
        require_equal(fs::prepare_standard_streams(table), -1,
                      "kdbg denies user standard-stream preparation");
        require(table.empty(), "kdbg refusal preserves the empty table");
    }
}

void test_standard_stream_objects_and_access() {
    PagePool<1> pages;
    const PageSnapshot before;
    fs::OpenFile closed{};
    require(!closed.can_read() && !closed.can_write(), "a default closed object has no capabilities");
    require_equal(closed.read_current({}), -1L, "closed objects reject empty reads");
    require_equal(closed.commit_read(0), -1, "closed objects reject empty read commits");
    require_equal(closed.write({}), -1L, "closed objects reject empty writes");

    fs::OpenFilePool pool{};
    require(pool.acquire_console(static_cast<fs::OpenFileAccess>(99)) == nullptr && pool.in_use() == 0,
            "invalid access cannot acquire a pool slot");
    fs::FileDescriptorTable first{};
    fs::FileDescriptorTable second{};
    require_equal(first.initialize_standard_stream(pool), 0, "prepare all three standard streams");
    const std::array<fs::OpenFileAccess, 3> modes{
        fs::OpenFileAccess::ReadOnly, fs::OpenFileAccess::WriteOnly, fs::OpenFileAccess::WriteOnly
    };
    for (int fd = TINYOS_STDIN_FILENO; fd <= TINYOS_STDERR_FILENO; ++fd) {
        const auto* file = first.get(fd);
        require(file != nullptr && file->kind() == fs::OpenFileKind::Console && file->file() == nullptr,
                "each standard descriptor owns a valid console object");
        require(file->access() == modes[static_cast<size_t>(fd)] && file->reference_count() == 1 &&
                file->offset() == 0, "each independent standard object has its requested mode and one reference");
        require_equal(file->can_read(), fd == TINYOS_STDIN_FILENO, "only initial stdin is readable");
        require_equal(file->can_write(), fd != TINYOS_STDIN_FILENO, "initial stdout and stderr are writable");
        for (int earlier = TINYOS_STDIN_FILENO; earlier < fd; ++earlier) {
            require(first.get(earlier) != file, "all three standard descriptors use distinct open objects");
        }
    }
    require_equal(second.initialize_standard_stream(pool), 0, "a fresh table prepares new standard objects");
    for (int fd = TINYOS_STDIN_FILENO; fd <= TINYOS_STDERR_FILENO; ++fd) {
        require(first.get(fd) != second.get(fd), "fresh tables do not share standard open objects");
    }
    require_equal(pool.in_use(), size_t{6}, "two fresh tables acquire six objects");
    const auto* about = fs::lookup_file({kAboutPath, sizeof(kAboutPath) - 1});
    require(about != nullptr, "the builtin remains available beside standard streams");
    require_equal(first.open(pool, *about), 3, "ordinary open follows the three occupied standard slots");
    require(first.get(3)->can_read() && !first.get(3)->can_write(), "builtin access remains read only");
    require_equal(second.open_console(pool), 3, "the legacy console open still uses a free descriptor");
    require(second.get(3)->access() == fs::OpenFileAccess::ReadWrite && second.get(3)->can_read() &&
            second.get(3)->can_write(), "a console opened without an access argument remains bidirectional");

    unsigned char byte = 0xa5;
    const std::array<unsigned char, 2> binary{0, 255};
    const auto polls = g_capture.reads;
    const auto writes = g_capture.writes;
    const auto output = g_capture.output_size;
    require_equal(first.get(TINYOS_STDIN_FILENO)->write({}), -1L, "stdin rejects empty writes");
    require_equal(first.get(TINYOS_STDIN_FILENO)->write(binary), -1L, "stdin rejects nonempty writes");
    for (int fd : {TINYOS_STDOUT_FILENO, TINYOS_STDERR_FILENO}) {
        require_equal(first.get(fd)->read_current({}), -1L, "output streams reject empty reads");
        require_equal(first.get(fd)->read_current({&byte, 1}), -1L, "output streams reject positive reads");
        require_equal(first.get(fd)->commit_read(0), -1, "output streams reject even empty read commits");
        require_equal(first.get(fd)->write({}), 0L, "writable empty spans succeed without output");
    }
    require_equal(first.get(TINYOS_STDIN_FILENO)->read_current({}), 0L, "stdin permits empty reads");
    require_equal(first.get(TINYOS_STDIN_FILENO)->commit_read(0), 0, "stdin permits empty read commits");
    require(byte == 0xa5 && g_capture.reads == polls && g_capture.writes == writes &&
            g_capture.output_size == output, "all empty and denied operations preserve input and output");
    for (int fd : {TINYOS_STDOUT_FILENO, TINYOS_STDERR_FILENO}) {
        require_equal(first.get(fd)->write(binary), 2L, "both output streams write the actual binary length");
    }
    require(g_capture.writes == writes + 2 && g_capture.output_size == output + 4 &&
            std::equal(binary.begin(), binary.end(), g_capture.output.begin() + output) &&
            std::equal(binary.begin(), binary.end(), g_capture.output.begin() + output + 2),
            "stdout and stderr share the device and preserve NUL and ff");
    auto* prior_stdin = first.get(TINYOS_STDIN_FILENO);
    first.close_all(pool);
    second.close_all(pool);
    require(pool.in_use() == 0 && !prior_stdin->can_read() && !prior_stdin->can_write(),
            "last close clears every object and its capabilities");
    before.expect_unchanged();
}

void test_standard_stream_preparation_rollback() {
    PagePool<1> pages;
    const PageSnapshot before;
    fs::OpenFilePool pool{};
    fs::FileDescriptorTable occupied{};
    require_equal(occupied.open_console(pool), 0, "prepare an existing descriptor before rejected initialization");
    const auto* original = occupied.get(0);
    require_equal(occupied.initialize_standard_stream(pool), -1, "initialization refuses a nonempty target");
    require(occupied.get(0) == original && original->reference_count() == 1 && pool.in_use() == 1,
            "nonempty refusal preserves the existing binding and reference");
    occupied.close_all(pool);

    const auto* about = fs::lookup_file({kAboutPath, sizeof(kAboutPath) - 1});
    require(about != nullptr, "prepare a real builtin object for the finite full-pool fixture");
    std::array<fs::OpenFile*, fs::OpenFilePool::kCapacity - 2> held{};
    for (auto& file : held) {
        file = pool.acquire(*about);
        require(file != nullptr, "hold all but two pool slots");
    }
    require_equal(held[0]->commit_read(1), 0, "an existing object's position is nonzero before rollback");
    fs::FileDescriptorTable target{};
    const auto polls = g_capture.reads;
    const auto writes = g_capture.writes;
    require_equal(target.initialize_standard_stream(pool), -1, "the third standard allocation fails with only two free slots");
    require(target.empty() && pool.in_use() == held.size(), "failure releases both partial standard objects");
    for (size_t i = 0; i < held.size(); ++i) {
        require(held[i]->kind() == fs::OpenFileKind::Builtin && held[i]->file() == about &&
                held[i]->access() == fs::OpenFileAccess::ReadOnly && held[i]->reference_count() == 1 &&
                held[i]->offset() == (i == 0 ? 1U : 0U), "rollback preserves every preexisting live object");
    }
    require(g_capture.reads == polls && g_capture.writes == writes, "preparation and rollback do not perform device I/O");
    pool.release(held.back());
    held.back() = nullptr;
    require_equal(target.initialize_standard_stream(pool), 0, "freeing one slot allows the complete three-object retry");
    require_equal(pool.in_use(), fs::OpenFilePool::kCapacity, "retry fills precisely the three available slots");
    target.close_all(pool);
    for (auto* file : held) {
        if (file != nullptr) {
            pool.release(file);
        }
    }
    require_equal(pool.in_use(), size_t{0}, "all fixture and standard-stream references are released");
    before.expect_unchanged();
}

void test_standard_stream_inheritance_holes_and_transfer() {
    PagePool<1> pages;
    const PageSnapshot before;
    fs::OpenFilePool pool{};
    fs::FileDescriptorTable parent{};
    fs::FileDescriptorTable child{};
    fs::FileDescriptorTable adopted{};
    fs::FileDescriptorTable fresh{};
    require_equal(parent.initialize_standard_stream(pool), 0, "prepare the parent's initial standard streams");
    const auto* about = fs::lookup_file({kAboutPath, sizeof(kAboutPath) - 1});
    require(about != nullptr, "the parent can open an ordinary file");
    require_equal(parent.open(pool, *about), 3, "the parent's ordinary file occupies descriptor three");
    require_equal(parent.close(pool, TINYOS_STDERR_FILENO), 0, "close stderr before inheritance");
    require_equal(child.inherit_from(pool, parent), 0, "inherit the exact table including its closed standard slot");
    require(child.get(TINYOS_STDERR_FILENO) == nullptr && pool.in_use() == 3,
            "inheritance preserves the hole and acquires no new open objects");
    for (int fd : {TINYOS_STDIN_FILENO, TINYOS_STDOUT_FILENO, 3}) {
        require(child.get(fd) == parent.get(fd) && parent.get(fd)->reference_count() == 2,
                "inheritance shares each existing object and increments its reference");
    }
    adopted.adopt(child);
    require(child.empty() && adopted.get(TINYOS_STDERR_FILENO) == nullptr &&
            adopted.get(TINYOS_STDIN_FILENO)->reference_count() == 2,
            "adopt transfers the inherited table without repairing holes or incrementing references");
    require_equal(fresh.initialize_standard_stream(pool), 0, "a separate fresh table receives complete new standard streams");
    require(fresh.get(3) == nullptr && fresh.get(TINYOS_STDERR_FILENO) != nullptr && pool.in_use() == 6,
            "fresh preparation does not copy the parent's ordinary file or closed slot");
    for (int fd : {TINYOS_STDIN_FILENO, TINYOS_STDOUT_FILENO}) {
        require(fresh.get(fd) != parent.get(fd) && fresh.get(fd)->reference_count() == 1,
                "fresh standard objects are independent of inherited objects");
    }
    const std::array<unsigned char, 1> input{'R'};
    supply(input);
    require_equal(console::poll_console_input(), 1L, "queue input before closing all console references");
    parent.close_all(pool);
    require_equal(adopted.get(TINYOS_STDIN_FILENO)->reference_count(), size_t{1},
                  "inherited stdin remains alive after its parent's close");
    adopted.close_all(pool);
    fresh.close_all(pool);
    require_equal(pool.in_use(), size_t{0}, "all standard objects close while the device remains alive");
    require_equal(parent.initialize_standard_stream(pool), 0, "fresh standard streams can reopen the existing device");
    unsigned char byte{};
    require_equal(parent.get(TINYOS_STDIN_FILENO)->read_current({&byte, 1}), 1L,
                  "closing all standard references preserves pending device input");
    require_equal(byte, static_cast<unsigned char>('R'), "the preserved input byte is unchanged");
    require_equal(parent.get(TINYOS_STDIN_FILENO)->commit_read(1), 0, "consume the preserved byte once");
    require_equal(parent.close(pool, TINYOS_STDIN_FILENO), 0, "standard input may be closed normally");
    require_equal(parent.open(pool, *about), TINYOS_STDIN_FILENO, "ordinary open reuses the closed standard number");
    require_equal(parent.get(TINYOS_STDIN_FILENO)->read_current({&byte, 1}), 1L,
                  "reading descriptor zero now uses the bound builtin object");
    require_equal(byte, kAbout[0], "descriptor reuse reads the builtin's first byte rather than the console");
    require_equal(parent.get(TINYOS_STDIN_FILENO)->commit_read(1), 0, "the reused builtin position advances normally");
    parent.close_all(pool);
    require_equal(pool.in_use(), size_t{0}, "descriptor reuse and final cleanup leave the pool empty");
    before.expect_unchanged();
}

void test_standard_stream_syscall_directions() {
    ProcessFixture fixture;
    const PageSnapshot before;
    fs::FileDescriptorTable pending{};
    require_equal(fs::prepare_standard_streams(pending), 0, "user-mode service prepares a complete standard table");
    fixture.process.adopt_file_descriptors(pending);
    require(pending.empty(), "Process adoption empties the prepared table");
    const std::array<unsigned char, 1> input{'S'};
    supply(input);
    std::memset(fixture.destination, 0xa5, 4);
    const auto polls = g_capture.reads;
    const auto writes = g_capture.writes;
    const auto output = g_capture.output_size;
    for (int fd : {TINYOS_STDOUT_FILENO, TINYOS_STDERR_FILENO}) {
        require_equal(fs::syscall_read(fixture.process, fd, 0, 0), -1L,
                      "output streams reject zero-length syscall reads");
        require_equal(fs::syscall_read(fixture.process, fd, ProcessFixture::kDestination, 1), -1L,
                      "output streams reject positive reads before polling or copying");
    }
    require_equal(fs::syscall_write_fd(fixture.process, TINYOS_STDIN_FILENO, 0, 0), -1L,
                  "stdin rejects zero-length syscall writes");
    require_equal(fs::syscall_write_fd(fixture.process, TINYOS_STDIN_FILENO, 0, 1), -1L,
                  "stdin rejects positive writes even with an invalid source");
    require_equal(fs::syscall_read(fixture.process, TINYOS_STDIN_FILENO, 0, 0), 0L,
                  "readable stdin permits zero length with an invalid destination");
    for (int fd : {TINYOS_STDOUT_FILENO, TINYOS_STDERR_FILENO}) {
        require_equal(fs::syscall_write_fd(fixture.process, fd, 0, 0), 0L,
                      "both writable streams permit zero length with an invalid source");
    }
    require(g_capture.reads == polls && g_capture.input_position == 0 && g_capture.writes == writes &&
            g_capture.output_size == output && fixture.destination[0] == 0xa5,
            "direction rejection and legal empty calls preserve hardware input, destination and output");
    require_equal(fs::syscall_read(fixture.process, TINYOS_STDIN_FILENO, ProcessFixture::kDestination, 1), 1L,
                  "a subsequent legal stdin read receives the untouched hardware byte");
    require_equal(fixture.destination[0], static_cast<unsigned char>('S'), "denied reads did not lose the original byte");
    const std::array<unsigned char, 3> binary{'X', 0, 255};
    std::copy(binary.begin(), binary.end(), fixture.source);
    require_equal(fs::syscall_write_fd(fixture.process, TINYOS_STDOUT_FILENO, ProcessFixture::kSource, 1), 1L,
                  "stdout writes exactly the requested byte");
    require_equal(fs::syscall_write_fd(fixture.process, TINYOS_STDERR_FILENO, ProcessFixture::kSource + 1, 2), 2L,
                  "stderr writes exactly the requested binary suffix");
    require(g_capture.output_size == output + 3 && g_capture.writes == writes + 2 &&
            std::equal(binary.begin(), binary.end(), g_capture.output.begin() + output),
            "standard syscall output preserves actual lengths and binary bytes");
    require_equal(fs::syscall_close(fixture.process, TINYOS_STDIN_FILENO), 0L, "stdin closes without being repaired");
    require_equal(fs::syscall_read(fixture.process, TINYOS_STDIN_FILENO, 0, 0), -1L,
                  "a closed standard descriptor rejects even zero length");
    before.expect_unchanged();
    fixture.finish();
}

void test_pool_kind_capacity_and_reuse() {
    PagePool<1> pages;
    const PageSnapshot before;
    const auto* about = fs::lookup_file({kAboutPath, sizeof(kAboutPath) - 1});
    require(about != nullptr, "the builtin file remains available");
    fs::OpenFilePool pool{};
    auto* device = pool.acquire_console();
    auto* builtin = pool.acquire(*about);
    require(device != nullptr && builtin != nullptr && device != builtin,
            "a console with a null file pointer cannot be overwritten by builtin acquisition");
    require(device->kind() == fs::OpenFileKind::Console && device->file() == nullptr &&
            device->reference_count() == 1 && device->offset() == 0, "console identity is defined by kind and references");
    require(builtin->kind() == fs::OpenFileKind::Builtin && builtin->file() == about,
            "ordinary acquisition publishes the builtin kind");
    require_equal(builtin->write({}), -1L, "a readonly builtin rejects even an empty write");
    pool.release(device);
    require(device->kind() == fs::OpenFileKind::None && device->reference_count() == 0,
            "the last release marks the pool slot free");
    auto* reused = pool.acquire(*about);
    require(reused == device && reused->kind() == fs::OpenFileKind::Builtin,
            "the first freed device slot can be reused for a regular file");
    pool.release(reused);
    pool.release(builtin);

    std::array<fs::OpenFile*, fs::OpenFilePool::kCapacity> entries{};
    for (size_t i = 0; i < entries.size(); ++i) {
        entries[i] = i % 2 == 0 ? pool.acquire_console() : pool.acquire(*about);
        require(entries[i] != nullptr && entries[i]->reference_count() == 1, "every mixed pool slot is available exactly once");
    }
    require(pool.acquire_console() == nullptr && pool.acquire(*about) == nullptr && pool.in_use() == entries.size(),
            "a full mixed pool rejects without changing existing objects");
    for (auto* entry : entries) {
        pool.release(entry);
    }
    require_equal(pool.in_use(), size_t{0}, "all mixed pool entries release cleanly");
    fs::FileDescriptorTable table{};
    for (size_t i = 0; i < fs::FileDescriptorTable::kCapacity; ++i) {
        require_equal(table.open_console(pool), static_cast<int>(i), "console open returns the actual lowest free FD");
    }
    require_equal(table.open_console(pool), -1, "a full FD table rejects without acquiring another pool entry");
    require_equal(pool.in_use(), fs::FileDescriptorTable::kCapacity, "full-table rejection preserves pool occupancy");
    table.close_all(pool);
    require(table.empty() && pool.in_use() == 0 && console::console_ready(), "closing FDs leaves the permanent device ready");
    before.expect_unchanged();
}

void test_independent_opens_and_inherited_references() {
    PagePool<1> pages;
    const PageSnapshot before;
    fs::OpenFilePool pool{};
    fs::FileDescriptorTable parent{};
    fs::FileDescriptorTable child{};
    require_equal(parent.open_console(pool), 0, "open the first independent device handle");
    require_equal(parent.open_console(pool), 1, "open a second independent device handle");
    require(parent.get(0) != parent.get(1), "independent opens have separate OpenFile objects");
    require_equal(child.inherit_from(pool, parent), 0, "inherit the parent's existing references");
    require(child.get(0) == parent.get(0) && child.get(1) == parent.get(1) &&
            parent.get(0)->reference_count() == 2, "inheritance shares objects and increases their references");
    const std::array<unsigned char, 3> source{'a', 'b', 'c'};
    supply(source);
    for (size_t i = 0; i < source.size(); ++i) {
        require_equal(console::poll_console_input(), 1L, "receive bytes into the one shared device stream");
    }
    unsigned char byte{};
    require_equal(parent.get(0)->read_current({&byte, 1}), 1L, "the first handle peeks from the shared stream");
    require_equal(byte, static_cast<unsigned char>('a'), "the first handle sees the oldest byte");
    require_equal(parent.get(0)->commit_read(1), 0, "consume the first delivered byte");
    require_equal(child.get(1)->read_current({&byte, 1}), 1L, "a different open object reads the same device stream");
    require_equal(byte, static_cast<unsigned char>('b'), "independent opens do not create independent input cursors");
    require_equal(child.get(1)->commit_read(1), 0, "consume through the second open object");
    require(parent.get(0)->offset() == 0 && child.get(1)->offset() == 0, "console consumption does not modify file offsets");
    parent.close_all(pool);
    require(child.get(0)->reference_count() == 1, "closing the parent keeps the child's references alive");
    child.close_all(pool);
    require(pool.in_use() == 0 && console::console_ready(), "last close frees open objects but not the device");
    auto* next = pool.acquire_console();
    require(next != nullptr, "reopen after the last device handle closes");
    require_equal(next->read_current({&byte, 1}), 1L, "last close preserves pending device input");
    require_equal(byte, static_cast<unsigned char>('c'), "reopening receives the original pending byte");
    require_equal(next->commit_read(1), 0, "drain the remaining stream byte");
    pool.release(next);
    before.expect_unchanged();
}

void test_exact_paths_and_builtin_syscalls() {
    ProcessFixture fixture;
    const PageSnapshot before;
    const std::array<std::string_view, 4> invalid{
        "/dev/consoleX", "/dev/console/", "/dev/Console", std::string_view{"/dev/console\0", 13}
    };
    for (auto path : invalid) {
        require_equal(fixture.open(path), -1L, "device path matching requires exact length and content");
    }
    const long device = fixture.open(kConsolePath);
    if (g_user_mode) {
        require_equal(device, 0L, "a copied exact path opens in user mode");
        require_equal(fs::syscall_close(fixture.process, device), 0L, "close the exact-path handle");
    } else {
        require_equal(device, -1L, "configured kdbg mode denies user console open");
    }
    require(fs::lookup_file({kConsolePath, sizeof(kConsolePath) - 1}) == nullptr,
            "device routing does not create a fake builtin file record");
    const long regular = fixture.open(kAboutPath);
    require_equal(regular, 0L, "builtin open remains available in both console modes");
    const auto reads = g_capture.reads;
    const auto writes = g_capture.writes;
    require_equal(fs::syscall_read(fixture.process, regular, 0, 0), 0L, "valid zero-length builtin read does not access a destination");
    require_equal(fs::syscall_read(fixture.process, regular, ProcessFixture::kDestination, 256), 18L,
                  "ordinary reads retain their exact short count");
    require(std::memcmp(fixture.destination, kAbout, sizeof(kAbout) - 1) == 0, "builtin read transfers its original bytes");
    require_equal(fs::syscall_read(fixture.process, regular, 0, 1), 0L, "builtin EOF remains zero instead of would-block");
    require_equal(fs::syscall_write_fd(fixture.process, regular, 0, 0), -1L, "readonly files reject zero-length FD writes");
    require_equal(fs::syscall_write_fd(fixture.process, regular, ProcessFixture::kSource, 1), -1L,
                  "readonly files reject nonempty FD writes");
    require(g_capture.reads == reads && g_capture.writes == writes, "ordinary file operations never poll or write the console");
    require_equal(fs::syscall_close(fixture.process, regular), 0L, "close the builtin reference");
    before.expect_unchanged();
    fixture.finish();
}

void test_console_read_failure_and_retry() {
    ProcessFixture fixture;
    const PageSnapshot before;
    const long fd = fixture.open(kConsolePath);
    require(fd >= 0, "open a console for transactional reads");
    supply({});
    const auto polls = g_capture.reads;
    require_equal(fs::syscall_read(fixture.process, fd, 0, 0), 0L, "zero-length console read succeeds with an invalid destination");
    require_equal(g_capture.reads, polls, "zero-length reads do not poll hardware");
    require_equal(fs::syscall_read(fixture.process, fd, 0, 1), long{SYSCALL_ERR_WOULD_BLOCK},
                  "positive-length temporarily empty input returns would-block before output validation");
    const std::array<unsigned char, 2> input{0, 255};
    supply(input);
    require_equal(fs::syscall_read(fixture.process, fd, ProcessFixture::kSource, 1), -1L,
                  "copying into the read-only user source page fails without consuming input");
    std::array<unsigned char, 2> pending{7, 7};
    require_equal(console::read_console_current(pending), 1L, "failed user delivery retains the received prefix");
    require_equal(pending[0], static_cast<unsigned char>(0), "the pending NUL byte survives failed copy");
    require_equal(fs::syscall_read(fixture.process, fd, ProcessFixture::kDestination, 1), 1L,
                  "legal retry receives the original byte while a second hardware byte is appended");
    require_equal(fixture.destination[0], static_cast<unsigned char>(0), "retry delivers the original NUL prefix");
    require_equal(fs::syscall_read(fixture.process, fd, ProcessFixture::kDestination + 1, 1), 1L,
                  "the next read receives the remaining appended byte");
    require_equal(fixture.destination[1], static_cast<unsigned char>(255), "ff remains data rather than an error sentinel");
    require_equal(console::read_console_current(pending), 0L, "only successfully copied bytes were consumed");
    require_equal(fs::syscall_read(fixture.process, -1, 0, 0), -1L, "invalid read descriptors are rejected before zero-length success");
    require_equal(fs::syscall_close(fixture.process, fd), 0L, "close the read descriptor");
    before.expect_unchanged();
    fixture.finish();
}

void test_console_write_exact_length_and_failed_copy() {
    ProcessFixture fixture;
    const PageSnapshot before;
    const long fd = fixture.open(kConsolePath);
    require(fd >= 0, "open a console for bounded writes");
    const std::array<unsigned char, 3> source{'X', 0, 255};
    std::copy(source.begin(), source.end(), fixture.source);
    const auto output_start = g_capture.output_size;
    const auto calls_start = g_capture.writes;
    require_equal(fs::syscall_write_fd(fixture.process, fd, 0, 0), 0L,
                  "a zero-length write neither reads an invalid source nor emits the backing array");
    require(g_capture.output_size == output_start && g_capture.writes == calls_start,
            "zero-length writes have no output callback side effect");
    require_equal(fs::syscall_write_fd(fixture.process, fd, ProcessFixture::kSource, 1), 1L,
                  "a one-byte request reports one byte rather than the 256-byte buffer capacity");
    require_equal(fs::syscall_write_fd(fixture.process, fd, ProcessFixture::kSource + 1, 2), 2L,
                  "nonempty binary writes use the exact requested length");
    require(g_capture.output_size == output_start + 3 && g_capture.writes == calls_start + 2 &&
            std::equal(source.begin(), source.end(), g_capture.output.begin() + output_start),
            "output contains exactly X, NUL and ff without zero-filled buffer tails");
    const auto output_before_failure = g_capture.output_size;
    const auto calls_before_failure = g_capture.writes;
    fixture.destination[SV39_PAGE_SIZE - 1] = 'P';
    require_equal(fs::syscall_write_fd(fixture.process, fd,
                  ProcessFixture::kDestination + SV39_PAGE_SIZE - 1, 2), -1L,
                  "copying a valid prefix followed by an unmapped page rejects the write");
    require(g_capture.output_size == output_before_failure && g_capture.writes == calls_before_failure,
            "failed complete copy emits none of the already copied prefix");
    require_equal(fs::syscall_write_fd(fixture.process, fd,
                  ProcessFixture::kDestination + SV39_PAGE_SIZE - 1, 1), 1L, "a legal write retry succeeds");
    require_equal(g_capture.output[output_before_failure], static_cast<unsigned char>('P'), "retry outputs the original valid source byte");
    require_equal(fs::syscall_write_fd(fixture.process, fd, ProcessFixture::kSource, SYSCALL_WRITE_MAX + 1), -1L,
                  "oversized writes reject before copying or output");
    require_equal(fs::syscall_write_fd(fixture.process, -1, 0, 0), -1L, "invalid write descriptors reject before empty success");
    require(!g_capture.overflow, "fixed callback storage covers the complete finite output");
    require_equal(fs::syscall_close(fixture.process, fd), 0L, "close the writer descriptor");
    before.expect_unchanged();
    fixture.finish();
}
}

int main(int argc, char** argv) {
    if (argc != 2 || (std::string_view{argv[1]} != "kdbg" && std::string_view{argv[1]} != "user")) {
        std::fprintf(stderr, "usage: console_fd_tests kdbg|user\n");
        return EXIT_FAILURE;
    }
    g_user_mode = std::string_view{argv[1]} == "user";
    bool passed = true;
    passed = run_case("console FD: one-time mode configuration and failed binding preservation", test_one_time_mode_configuration) && passed;
    passed = run_case("console FD: kind, mixed capacity and last-close slot reuse", test_pool_kind_capacity_and_reuse) && passed;
    passed = run_case("console FD: independent opens, inheritance and permanent stream lifetime", test_independent_opens_and_inherited_references) && passed;
    passed = run_case("console FD: exact device paths, mode gate and unchanged builtin syscalls", test_exact_paths_and_builtin_syscalls) && passed;
    passed = run_case("standard streams: independent objects and access modes", test_standard_stream_objects_and_access) && passed;
    passed = run_case("standard streams: partial preparation rollback and retry", test_standard_stream_preparation_rollback) && passed;
    passed = run_case("standard streams: inherited holes, transfer and descriptor reuse", test_standard_stream_inheritance_holes_and_transfer) && passed;
    if (g_user_mode) {
        passed = run_case("console FD: would-block, zero length, failed copy and retry", test_console_read_failure_and_retry) && passed;
        passed = run_case("console FD: exact write length, binary output and failed copy", test_console_write_exact_length_and_failed_copy) && passed;
        passed = run_case("standard streams: syscall directions precede side effects", test_standard_stream_syscall_directions) && passed;
    }
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

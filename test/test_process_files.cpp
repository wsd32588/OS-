#include "process/process.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "fs/file_service.hpp"
#include "fs/file_syscall.hpp"
#include "fs/open_file.hpp"
#include "user_memory_fixture.hpp"

namespace {
using namespace tinyos_test;
using tinyos::Process;
using tinyos::fs::FileDescriptorTable;

constexpr char kAboutPath[] = "/etc/about";
constexpr unsigned char kAbout[] = "TinyOS builtin fs\n";

std::span<const char> about_path() {
    return {kAboutPath, sizeof(kAboutPath) - 1};
}

void test_files_block_reset_and_preserve_metadata() {
    require(tinyos::fs::file_service_ready(), "earlier binding test configured the permanent provider");
    Process process;
    process.set_parent_id(77);
    process.record_exit({TASK_EXIT_FAULT, 0, 13});
    require(process.files_empty() && process.resources_empty(), "new Process owns no file references");
    require_equal(process.open_file({}), -1, "empty path rejects without acquiring a descriptor");
    constexpr char missing[] = "/missing";
    require_equal(process.open_file({missing, sizeof(missing) - 1}), -1,
                  "unknown path rejects without acquiring a descriptor");
    const auto allocations = host_allocation_calls();
    const auto calls = host_call_counts();
    const int fd = process.open_file(about_path());
    require_equal(fd, 0, "first Process open uses fd zero");
    auto* opened = process.find_open_file(fd);
    const Process& view = process;
    require(opened != nullptr && view.find_open_file(fd) == opened,
            "const and mutable Process lookup borrow the same object");
    require(process.find_open_file(-1) == nullptr && view.find_open_file(8) == nullptr,
            "Process lookup preserves descriptor bounds");
    require_equal(opened->commit_read(7), 0, "give the open object a nonzero position");
    require(!process.resources_empty(), "open descriptors count as Process resources");
    require_equal(process.reset(), -1, "Process reset refuses owned file references");
    require(process.parent_id() == 77 && process.exit_info().reason == TASK_EXIT_FAULT &&
            process.exit_info().fault_scause == 13 && process.find_open_file(fd) == opened &&
            opened->offset() == 7 && opened->reference_count() == 1,
            "failed reset preserves metadata, position and references");
    process.close_files();
    require(process.files_empty() && process.resources_empty(), "closing files makes an addressless Process empty");
    require(process.parent_id() == 77 && process.exit_info().reason == TASK_EXIT_FAULT,
            "closing files does not consume the Process exit record");
    require_equal(process.reset(), 0, "reset succeeds after references are closed");
    require(process.parent_id() == tinyos::kNoParentId &&
            process.exit_info().reason == TASK_EXIT_NONE && process.exit_info().fault_scause == 0,
            "successful reset clears parent and exit metadata");
    require_equal(host_allocation_calls(), allocations, "Process file operations allocate no physical pages");
    require_equal(host_call_counts(), calls, "Process file operations do not alter page ownership");
}

void test_clone_adopt_and_failed_inheritance() {
    Process parent;
    Process child;
    for (int fd = 0; fd < 3; ++fd) {
        require_equal(parent.open_file(about_path()), fd, "prepare Process descriptors");
    }
    require_equal(parent.close_file(1), 0, "leave a descriptor hole before cloning");
    auto* shared = parent.find_open_file(0);
    require_equal(shared->commit_read(7), 0, "parent starts at the middle of the file");
    FileDescriptorTable pending;
    require_equal(parent.clone_file_descriptors(pending), 0, "prepare retained references before publication");
    require(pending.get(0) == shared && pending.get(1) == nullptr &&
            pending.get(2) == parent.find_open_file(2) && shared->reference_count() == 2,
            "clone shares original objects and preserves fd holes");
    child.adopt_file_descriptors(pending);
    require(pending.empty() && child.find_open_file(0) == shared && child.find_open_file(1) == nullptr &&
            shared->reference_count() == 2, "adopt moves pending references without a second retain");

    const int unrelated = tinyos::fs::open_file(pending, about_path());
    require_equal(unrelated, 0, "prepare an already occupied output candidate");
    auto* original_output = pending.get(0);
    require_equal(parent.clone_file_descriptors(pending), -1, "cloning into nonempty output is rejected");
    require(pending.get(0) == original_output && original_output->reference_count() == 1 &&
            shared->reference_count() == 2 && shared->offset() == 7 &&
            parent.find_open_file(0) == shared && child.find_open_file(0) == shared,
            "failed clone preserves output, source and other sharers");
    tinyos::fs::close_all_files(pending);
    require_equal(parent.clone_file_descriptors(pending), 0, "empty candidate can be retried");
    require_equal(shared->reference_count(), std::size_t{3}, "retry adds exactly one prepared reference");
    tinyos::fs::close_all_files(pending);
    require_equal(shared->reference_count(), std::size_t{2}, "discarding a candidate rolls its reference back");
    parent.close_files();
    require(child.find_open_file(0) == shared && shared->reference_count() == 1,
            "closing the parent leaves the child's reference alive");
    std::array<unsigned char, 11> tail{};
    require_equal(shared->read_current(tail), 11L, "child resumes the inherited shared position");
    require_equal(std::memcmp(tail.data(), kAbout + 7, tail.size()), 0, "inherited tail bytes are exact");
    require_equal(shared->commit_read(tail.size()), 0, "child commits the shared read count");
    child.close_files();
    require(parent.resources_empty() && child.resources_empty(), "all adopted references are closed");
    require_equal(parent.reset(), 0, "parent can reset after close");
    require_equal(child.reset(), 0, "child can reset after close");
}

class ProcessMemoryFixture {
public:
    static constexpr uintptr_t kPathAddress = 0x40000000;
    static constexpr uintptr_t kOutputAddress = kPathAddress + SV39_PAGE_SIZE;

    ProcessMemoryFixture() {
        host_set_satp(0);
        kernel_root_ = sv39_create_page_table();
        require(kernel_root_ != nullptr, "allocate a real borrowed template root");
        for (std::size_t i = 0; i < shared_tables_.size(); ++i) {
            shared_tables_[i] = sv39_create_page_table();
            require(shared_tables_[i] != nullptr, "allocate a real shared template branch");
            kernel_root_->entries[i * 2] = sv39_make_pte(
                reinterpret_cast<uintptr_t>(shared_tables_[i]), SV39_PTE_V);
        }
    }

    void adopt_into(Process& process, bool with_buffers) {
        UserAddressSpace candidate{};
        require_equal(user_address_space_create(kernel_root_, &candidate), SV39_OK,
                      "create a real owned private address space");
        if (with_buffers) {
            candidate.user_memory.root = candidate.root;
            for (std::size_t i = 0; i < 2; ++i) {
                void* page = pmm_alloc_page();
                require(page != nullptr, "allocate an owned user content page");
                pages_[i] = static_cast<unsigned char*>(page);
                std::memset(page, 0xcc, SV39_PAGE_SIZE);
                const uintptr_t address = kPathAddress + i * SV39_PAGE_SIZE;
                candidate.user_memory.pages[i] = {address, page};
                ++candidate.user_memory.page_count;
                const uint64_t flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_A |
                    (i == 0 ? 0 : SV39_PTE_W | SV39_PTE_D);
                require_equal(sv39_map_page(candidate.root, address,
                    reinterpret_cast<uintptr_t>(page), flags), SV39_OK, "map the owned user page");
            }
            std::memcpy(pages_[0], kAboutPath, sizeof(kAboutPath) - 1);
        }
        process.adopt(candidate);
        require(candidate.root == nullptr && process.root() != nullptr,
                "Process accepts unique ownership and clears the source");
    }

    unsigned char* output() const { return pages_[1]; }

    void release_template() {
        require_equal(pmm_available_pages(), std::size_t{12 - 3}, "only borrowed template storage remains");
        for (auto* table : shared_tables_) {
            require_equal(pmm_free_page(table), 0, "free fixture template branch");
        }
        require_equal(pmm_free_page(kernel_root_), 0, "free fixture template root");
        require_equal(pmm_available_pages(), std::size_t{12}, "all fixture physical pages are restored");
    }

private:
    PagePool<12> pages_pool_;
    Sv39PageTable* kernel_root_{};
    std::array<Sv39PageTable*, 2> shared_tables_{};
    std::array<unsigned char*, 2> pages_{};
};

void test_releasing_address_space_preserves_files() {
    ProcessMemoryFixture fixture;
    Process process;
    fixture.adopt_into(process, false);
    process.set_parent_id(88);
    process.record_exit({TASK_EXIT_REQUESTED, 23, 0});
    const int fd = process.open_file(about_path());
    require(fd >= 0, "open before address-space release");
    auto* opened = process.find_open_file(fd);
    require_equal(opened->commit_read(7), 0, "retain a nonzero position across release");
    const auto available = pmm_available_pages();
    const auto frees = host_call_counts().free;
    require_equal(process.release_address_space(), SV39_OK, "release the real private root");
    require(process.root() == nullptr && process.find_open_file(fd) == opened &&
            opened->reference_count() == 1 && opened->offset() == 7,
            "address-space release preserves file references and positions");
    require_equal(pmm_available_pages(), available + 1, "the owned private root was actually freed");
    require_equal(host_call_counts().free, frees + 1, "exactly the private root was returned");
    require(!process.resources_empty() && process.reset() == -1 && process.parent_id() == 88 &&
            process.exit_info().status == 23, "files still block reset after the address space is gone");
    process.close_files();
    require_equal(process.reset(), 0, "reset succeeds after explicit file closure");
    fixture.release_template();
}

void test_typed_read_commits_after_real_copy() {
    ProcessMemoryFixture fixture;
    Process process;
    fixture.adopt_into(process, true);
    const long fd = tinyos::fs::syscall_open_file(
        process, ProcessMemoryFixture::kPathAddress, sizeof(kAboutPath) - 1);
    require_equal(fd, 0L, "typed OPEN copies the real user path and opens the file");
    const auto allocations = host_allocation_calls();
    const auto calls = host_call_counts();
    require_equal(tinyos::fs::syscall_read(process, fd, ProcessMemoryFixture::kOutputAddress, 7), 7L,
                  "typed READ copies the prefix and commits seven bytes");
    auto* opened = process.find_open_file(static_cast<int>(fd));
    require_equal(opened->offset(), std::size_t{7}, "successful copy advances the open position");
    require_equal(tinyos::fs::syscall_read(process, fd, 0, 11), -1L,
                  "unmapped output rejects the real user copy");
    require(opened->offset() == 7 && opened->reference_count() == 1,
            "failed copy preserves shared position and reference count");
    require_equal(tinyos::fs::syscall_read(process, fd, ProcessMemoryFixture::kOutputAddress + 7, 11), 11L,
                  "valid retry reads the same uncommitted tail");
    require_equal(std::memcmp(fixture.output(), kAbout, sizeof(kAbout) - 1), 0,
                  "real mapped reads reconstruct the original contents");
    require_equal(fixture.output()[sizeof(kAbout) - 1], 0xcc, "typed read preserves the output tail guard");
    require_equal(tinyos::fs::syscall_read(process, fd, 0, 1), 0L, "EOF does not touch the output");
    require_equal(tinyos::fs::syscall_read(process, fd, 0, 0), 0L, "zero length keeps the current position");
    require_equal(host_allocation_calls(), allocations, "typed file operations do not allocate physical pages");
    require_equal(host_call_counts(), calls, "typed reads do not unmap, flush or free pages");
    require_equal(tinyos::fs::syscall_close(process, fd), 0L, "typed close releases its descriptor");
    require_equal(process.release_address_space(), SV39_OK, "release all genuinely owned mapped pages");
    require_equal(process.reset(), 0, "closed and released Process can reset");
    fixture.release_template();
}
} // namespace

int test_process_files() {
    bool passed = true;
    passed = run_case("process files: references block reset and preserve metadata",
                      test_files_block_reset_and_preserve_metadata) && passed;
    passed = run_case("process files: clone and adopt share references with transactional failure",
                      test_clone_adopt_and_failed_inheritance) && passed;
    passed = run_case("process files: address-space release keeps open files alive",
                      test_releasing_address_space_preserves_files) && passed;
    passed = run_case("process files: typed read commits after successful mapped copy",
                      test_typed_read_commits_after_real_copy) && passed;
    return passed ? 0 : -1;
}

#include "fs/file_syscall.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "fs/builtin_fs.hpp"
#include "fs/file_service.hpp"
#include "user_memory_fixture.hpp"

namespace {
using namespace tinyos_test;
using tinyos::fs::BuiltinFile;
using tinyos::fs::BuiltinFileSystem;
using tinyos::fs::configure_file_service;

constexpr char kPath[] = "/etc/about";
constexpr char kEmptyPath[] = "/empty";
constexpr unsigned char kAbout[] = "TinyOS builtin fs\n";
constexpr unsigned char kReplacement[] = "different filesystem";
constexpr unsigned char kGuard = 0xcc;
constexpr std::size_t kPathOffset = 32;
constexpr std::size_t kOutputOffset = 128;

// Binding is process-wide and borrowed. Both possible providers and their byte
// storage outlive every test; no reset hook is added to the production service.
BuiltinFileSystem g_filesystem;
BuiltinFileSystem g_replacement_filesystem;

void prepare_user_buffers(UserMemoryFixture& fixture) {
    auto* code = static_cast<unsigned char*>(fixture.memory.pages[0].physical_page);
    auto* stack = static_cast<unsigned char*>(fixture.memory.pages[1].physical_page);
    std::memcpy(code + kPathOffset, kPath, sizeof(kPath) - 1);
    std::memset(stack, kGuard, SV39_PAGE_SIZE);
}

long read_checked(const UserMemoryFixture& fixture, uint64_t offset,
                  uintptr_t destination, uint64_t length) {
    const unsigned long available = pmm_available_pages();
    const unsigned long allocations = host_allocation_calls();
    const auto calls = host_call_counts();
    const Mapping code_mapping = query_mapping(fixture.root, UserMemoryFixture::code_address);
    const Mapping stack_mapping = query_mapping(fixture.root, UserMemoryFixture::stack_address);
    const long result = fs_syscall_read_file(
        fixture.root, UserMemoryFixture::code_address + kPathOffset,
        sizeof(kPath) - 1, offset, destination, length);
    require_equal(pmm_available_pages(), available, "READ_FILE preserves available pages");
    require_equal(host_allocation_calls(), allocations, "READ_FILE performs no physical allocation");
    require_equal(host_call_counts(), calls, "READ_FILE does not unmap, flush or free pages");
    require_equal(query_mapping(fixture.root, UserMemoryFixture::code_address), code_mapping,
                  "READ_FILE preserves the path mapping");
    require_equal(query_mapping(fixture.root, UserMemoryFixture::stack_address), stack_mapping,
                  "READ_FILE preserves the output mapping");
    const auto* code = static_cast<const unsigned char*>(fixture.memory.pages[0].physical_page);
    require_equal(std::memcmp(code + kPathOffset, kPath, sizeof(kPath) - 1), 0,
                  "READ_FILE preserves user path bytes");
    return result;
}

void test_binding_lifecycle_and_segmented_read() {
    UserMemoryFixture fixture;
    prepare_user_buffers(fixture);
    auto* output = static_cast<unsigned char*>(fixture.memory.pages[1].physical_page);
    const uintptr_t destination = UserMemoryFixture::stack_address + kOutputOffset;
    require_equal(read_checked(fixture, 0, destination, sizeof(kAbout) - 1), -1L,
                  "READ_FILE rejects calls before a filesystem is bound");
    require_equal(output[kOutputOffset], kGuard, "unbound read leaves the output untouched");
    require_equal(configure_file_service(g_filesystem), -1,
                  "binding an uninitialized provider fails without consuming configuration");
    const BuiltinFile files[] = {
        {{kPath, sizeof(kPath) - 1}, {kAbout, sizeof(kAbout) - 1}},
        {{kEmptyPath, sizeof(kEmptyPath) - 1}, {}}
    };
    require_equal(g_filesystem.init(files), 0, "initialize the permanent test provider");
    require_equal(configure_file_service(g_filesystem), 0,
                  "binding can retry after provider initialization");
    require_equal(read_checked(fixture, 0, destination, 7), 7L,
                  "first READ_FILE transfers seven bytes through real user mappings");
    require_equal(read_checked(fixture, 7, destination + 7, 11), 11L,
                  "second READ_FILE transfers the remaining eleven bytes");
    require_equal(std::memcmp(output + kOutputOffset, kAbout, sizeof(kAbout) - 1), 0,
                  "segmented syscall reads reconstruct the original about file");
    require(output[kOutputOffset - 1] == kGuard &&
            output[kOutputOffset + sizeof(kAbout) - 1] == kGuard,
            "successful reads preserve adjacent output guards");
    const BuiltinFile* found = g_filesystem.lookup({kPath, sizeof(kPath) - 1});
    require(found != nullptr && found->bytes.data() == kAbout &&
            found->bytes.size() == sizeof(kAbout) - 1,
            "reading preserves the provider's borrowed source descriptor");
}

void test_repeated_binding_preserves_original_provider() {
    const BuiltinFile replacement[] = {
        {{kPath, sizeof(kPath) - 1}, {kReplacement, sizeof(kReplacement) - 1}}
    };
    require_equal(g_replacement_filesystem.init(replacement), 0,
                  "initialize a distinct permanent provider");
    const BuiltinFile* original = g_filesystem.lookup({kPath, sizeof(kPath) - 1});
    require_equal(configure_file_service(g_replacement_filesystem), -1,
                  "second provider binding is rejected");
    require_equal(configure_file_service(g_filesystem), -1,
                  "rebinding the same provider is also rejected");
    require(g_filesystem.lookup({kPath, sizeof(kPath) - 1}) == original,
            "repeat binding preserves the original stored record address");
    UserMemoryFixture fixture;
    prepare_user_buffers(fixture);
    const uintptr_t destination = UserMemoryFixture::stack_address + kOutputOffset;
    require_equal(read_checked(fixture, 0, destination, sizeof(kAbout) - 1),
                  static_cast<long>(sizeof(kAbout) - 1), "original provider remains in service");
    const auto* output = static_cast<const unsigned char*>(fixture.memory.pages[1].physical_page);
    require_equal(std::memcmp(output + kOutputOffset, kAbout, sizeof(kAbout) - 1), 0,
                  "rejected provider cannot replace the file contents");
}

void test_short_count_and_no_output_access() {
    UserMemoryFixture fixture;
    prepare_user_buffers(fixture);
    auto* output = static_cast<unsigned char*>(fixture.memory.pages[1].physical_page);
    const uintptr_t destination = UserMemoryFixture::stack_address + SV39_PAGE_SIZE - 2;
    require_equal(read_checked(fixture, sizeof(kAbout) - 3, destination, 256), 2L,
                  "only two remaining bytes are validated and copied at the end of a writable page");
    require(output[SV39_PAGE_SIZE - 3] == kGuard &&
            output[SV39_PAGE_SIZE - 2] == 's' && output[SV39_PAGE_SIZE - 1] == '\n',
            "short syscall read copies its tail without touching the preceding guard");
    require_equal(read_checked(fixture, sizeof(kAbout) - 1, 0, 1), 0L,
                  "EOF returns zero without checking the destination");
    require_equal(read_checked(fixture, UINT64_MAX, 0, 1), 0L,
                  "extreme offset returns zero without checking the destination");
    require_equal(read_checked(fixture, 0, 0, 0), 0L,
                  "known path zero-length read permits an invalid destination");
}
} // namespace

int test_file_syscall() {
    if (!run_case("file syscall: initial binding rejection, retry and real segmented copy",
                  test_binding_lifecycle_and_segmented_read)) {
        return -1;
    }
    if (!run_case("file syscall: repeated binding preserves the original provider",
                  test_repeated_binding_preserves_original_provider)) {
        return -1;
    }
    if (!run_case("file syscall: short count and no output access for zero-result reads",
                  test_short_count_and_no_output_access)) {
        return -1;
    }
    return 0;
}

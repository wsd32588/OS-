#include "fs/open_file.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

#include "fs/builtin_fs.hpp"
#include "fs/file_descriptor_table.hpp"
#include "host_support.hpp"
#include "runtime/panic.h"
#include "test_support.hpp"

// A violated model invariant must terminate even when the caller is noexcept.
// The target's UART/wfi implementation is deliberately not linked on the host.
extern "C" [[noreturn]] void kernel_panic(const char* file, int line, const char* message) {
    std::fprintf(stderr, "[HOST PANIC] %s:%d: %s\n", file, line, message);
    std::abort();
}

namespace {
using namespace tinyos_test;
using tinyos::fs::BuiltinFile;
using tinyos::fs::BuiltinFileSystem;
using tinyos::fs::FileDescriptorTable;
using tinyos::fs::OpenFile;
using tinyos::fs::OpenFilePool;

constexpr unsigned char kAbout[] = "TinyOS builtin fs\n";
constexpr char kAboutPath[] = "/etc/about";
constexpr char kEmptyPath[] = "/empty";

static_assert(!std::is_copy_constructible_v<OpenFile> && !std::is_move_constructible_v<OpenFile>);
static_assert(!std::is_copy_assignable_v<OpenFile> && !std::is_move_assignable_v<OpenFile>);
static_assert(
    !std::is_copy_constructible_v<OpenFilePool> && !std::is_move_constructible_v<OpenFilePool>
);
static_assert(!std::is_copy_assignable_v<OpenFilePool> && !std::is_move_assignable_v<OpenFilePool>);
static_assert(
    !std::is_copy_constructible_v<FileDescriptorTable> &&
    !std::is_move_constructible_v<FileDescriptorTable>
);
static_assert(
    !std::is_copy_assignable_v<FileDescriptorTable> &&
    !std::is_move_assignable_v<FileDescriptorTable>
);
static_assert(
    std::is_same_v<decltype(std::declval<const FileDescriptorTable&>().get(0)), const OpenFile*>
);

struct Fixture {
    BuiltinFileSystem filesystem;
    OpenFilePool pool;

    Fixture() {
        const BuiltinFile files[] = {
            {{kAboutPath, sizeof(kAboutPath) - 1}, {kAbout, sizeof(kAbout) - 1}},
            {{kEmptyPath, sizeof(kEmptyPath) - 1}, {}}
        };
        require_equal(filesystem.init(files), 0, "initialize real file records for the model");
    }

    const BuiltinFile& about() const {
        const auto* result = filesystem.lookup({kAboutPath, sizeof(kAboutPath) - 1});
        require(result != nullptr, "about record exists");
        return *result;
    }

    const BuiltinFile& empty_file() const {
        const auto* result = filesystem.lookup({kEmptyPath, sizeof(kEmptyPath) - 1});
        require(result != nullptr, "empty record exists");
        return *result;
    }
};

struct OpenState {
    const BuiltinFile* file{};
    std::size_t offset{};
    std::size_t references{};
    bool operator==(const OpenState&) const = default;
};

OpenState capture(const OpenFile* file) {
    return file == nullptr ? OpenState{}
                           : OpenState{file->file(), file->offset(), file->reference_count()};
}

struct TableState {
    std::array<const OpenFile*, FileDescriptorTable::kCapacity> entries{};
    std::array<OpenState, FileDescriptorTable::kCapacity> objects{};
    std::size_t pool_usage{};
    bool operator==(const TableState&) const = default;
};

TableState capture(const FileDescriptorTable& table, const OpenFilePool& pool) {
    TableState result{};
    for (std::size_t i = 0; i < result.entries.size(); ++i) {
        result.entries[i] = table.get(static_cast<int>(i));
        result.objects[i] = capture(result.entries[i]);
    }
    result.pool_usage = pool.in_use();
    return result;
}

void test_read_prepare_commit_and_empty_file() {
    Fixture fixture;
    OpenFile unopened;
    std::array<unsigned char, 20> output{};
    output.fill(0xcc);
    require_equal(unopened.read_current(output), -1L, "unused OpenFile rejects reading");
    require_equal(unopened.commit_read(0), -1, "unused OpenFile rejects committing");
    OpenFile* opened = fixture.pool.acquire(fixture.about());
    require(
        opened != nullptr && opened->file() == &fixture.about() && opened->offset() == 0 &&
            opened->reference_count() == 1,
        "acquire begins at offset zero with one reference"
    );
    require_equal(
        opened->read_current(std::span(output).subspan(1, 7)),
        7L,
        "prepare first seven bytes"
    );
    require_equal(std::memcmp(output.data() + 1, kAbout, 7), 0, "prepared bytes match the file");
    require(output.front() == 0xcc && output[8] == 0xcc, "prepare preserves adjacent guards");
    require_equal(opened->offset(), std::size_t{0}, "preparing bytes does not advance position");
    require_equal(
        opened->read_current(std::span(output).subspan(1, 7)),
        7L,
        "another prepare still starts at the same position"
    );
    require_equal(opened->commit_read(7), 0, "commit advances the actual first read count");
    const auto before = capture(opened);
    require_equal(opened->commit_read(sizeof(kAbout)), -1, "commit cannot exceed remaining bytes");
    require_equal(opened->commit_read(SIZE_MAX), -1, "overflowing commit is rejected");
    require_equal(capture(opened), before, "invalid commits preserve the complete open object");
    require_equal(opened->read_current(output), 11L, "prepare short tail count");
    require_equal(opened->offset(), std::size_t{7}, "short prepare still does not commit");
    require_equal(opened->commit_read(11), 0, "commit the actual short count");
    output.fill(0xcc);
    require_equal(opened->read_current(output), 0L, "EOF prepares zero bytes");
    require_equal(output.front(), 0xcc, "EOF leaves output untouched");
    require_equal(opened->commit_read(0), 0, "zero-byte EOF commit is legal");
    require_equal(opened->commit_read(1), -1, "positive commit past EOF is rejected");
    fixture.pool.release(opened);
    require_equal(fixture.pool.in_use(), std::size_t{0}, "last reference releases the object");
    OpenFile* empty = fixture.pool.acquire(fixture.empty_file());
    require(
        empty == opened && empty->offset() == 0 && empty->reference_count() == 1,
        "released pool storage can be acquired with fresh state"
    );
    require_equal(empty->read_current(output), 0L, "empty file prepares zero bytes");
    require_equal(empty->commit_read(0), 0, "empty file permits zero commit");
    require_equal(empty->commit_read(1), -1, "empty file rejects positive commit");
    fixture.pool.release(empty);
}

void test_independent_opens_have_independent_positions() {
    Fixture fixture;
    FileDescriptorTable table;
    require_equal(table.open(fixture.pool, fixture.about()), 0, "first open returns legal fd zero");
    require_equal(table.open(fixture.pool, fixture.about()), 1, "second open returns fd one");
    OpenFile* first = table.get(0);
    OpenFile* second = table.get(1);
    require(
        first != second && first->file() == second->file(),
        "independent opens share only the file record"
    );
    std::array<unsigned char, 7> bytes{};
    require_equal(first->read_current(bytes), 7L, "prepare the first independent handle");
    require_equal(first->commit_read(7), 0, "advance only the first handle");
    require_equal(second->offset(), std::size_t{0}, "second open remains at the beginning");
    require_equal(second->read_current(bytes), 7L, "second handle reads its own prefix");
    require_equal(std::memcmp(bytes.data(), kAbout, bytes.size()), 0, "second prefix is unchanged");
    require_equal(table.close(fixture.pool, 0), 0, "close the first independent handle");
    require(
        table.get(0) == nullptr && second->reference_count() == 1 && second->offset() == 0,
        "closing one independent handle preserves the other"
    );
    table.close_all(fixture.pool);
    require(table.empty() && fixture.pool.in_use() == 0, "all independent opens close cleanly");
}

void test_descriptor_bounds_capacity_and_reuse() {
    Fixture fixture;
    FileDescriptorTable table;
    for (int fd = 0; fd < static_cast<int>(FileDescriptorTable::kCapacity); ++fd) {
        require_equal(
            table.open(fixture.pool, fixture.about()),
            fd,
            "open fills the minimum free fd"
        );
    }
    const auto before = capture(table, fixture.pool);
    const FileDescriptorTable& view = table;
    require(
        table.get(-1) == nullptr && table.get(8) == nullptr && view.get(-1) == nullptr &&
            view.get(8) == nullptr,
        "both get variants reject out-of-range descriptors"
    );
    require_equal(table.close(fixture.pool, -1), -1, "negative close is rejected");
    require_equal(table.close(fixture.pool, 8), -1, "upper-bound close is rejected");
    require_equal(table.open(fixture.pool, fixture.about()), -1, "ninth descriptor is rejected");
    require_equal(
        capture(table, fixture.pool),
        before,
        "descriptor rejections preserve entries, positions and references"
    );
    const OpenFile* prior = table.get(3);
    require_equal(table.close(fixture.pool, 3), 0, "close an interior fd");
    const auto hole = capture(table, fixture.pool);
    require_equal(table.close(fixture.pool, 3), -1, "duplicate close is rejected");
    require_equal(
        capture(table, fixture.pool),
        hole,
        "duplicate close preserves the remaining objects"
    );
    require_equal(
        table.open(fixture.pool, fixture.about()),
        3,
        "smallest descriptor hole is reused"
    );
    require(
        table.get(3) == prior && table.get(3)->offset() == 0 &&
            table.get(3)->reference_count() == 1,
        "reopened storage starts with fresh position and reference count"
    );
    table.close_all(fixture.pool);
    const auto closed = capture(table, fixture.pool);
    table.close_all(fixture.pool);
    require(table.empty() && fixture.pool.in_use() == 0, "close_all releases all eight objects");
    require_equal(capture(table, fixture.pool), closed, "close_all is idempotent");
}

void test_inheritance_shares_objects_and_preserves_holes() {
    Fixture fixture;
    FileDescriptorTable parent;
    FileDescriptorTable child;
    for (int fd = 0; fd < 3; ++fd) {
        require_equal(parent.open(fixture.pool, fixture.about()), fd, "create parent descriptors");
    }
    require_equal(parent.close(fixture.pool, 1), 0, "create an inherited descriptor hole");
    require_equal(parent.get(0)->commit_read(7), 0, "parent position before inheritance");
    const auto before = capture(parent, fixture.pool);
    require_equal(parent.inherit_from(fixture.pool, parent), -1, "self inheritance is rejected");
    require_equal(capture(parent, fixture.pool), before, "self inheritance changes no references");
    require_equal(child.inherit_from(fixture.pool, parent), 0, "inherit into an empty table");
    require(
        child.get(0) == parent.get(0) && child.get(1) == nullptr && child.get(2) == parent.get(2),
        "inherit preserves original fd numbers and holes"
    );
    require(
        child.get(0)->reference_count() == 2 && child.get(2)->reference_count() == 2 &&
            fixture.pool.in_use() == 2,
        "inherit retains existing objects without acquiring new pool entries"
    );
    const auto inherited = capture(child, fixture.pool);
    require_equal(
        child.inherit_from(fixture.pool, parent),
        -1,
        "nonempty target inheritance is rejected"
    );
    require_equal(
        capture(child, fixture.pool),
        inherited,
        "rejected inheritance preserves positions and references"
    );
    std::array<unsigned char, 11> tail{};
    require_equal(
        child.get(0)->read_current(tail),
        11L,
        "child observes the inherited parent offset"
    );
    require_equal(
        std::memcmp(tail.data(), kAbout + 7, tail.size()),
        0,
        "child reads the expected tail"
    );
    require_equal(child.get(0)->commit_read(tail.size()), 0, "child commits the shared position");
    require_equal(
        parent.get(0)->offset(),
        std::size_t{18},
        "parent sees the child's committed position"
    );
    require_equal(parent.close(fixture.pool, 0), 0, "parent closes one shared descriptor");
    require(
        child.get(0)->reference_count() == 1 && child.get(0)->file() == &fixture.about(),
        "child keeps the file alive after its parent closes it"
    );
    parent.close_all(fixture.pool);
    require(
        child.get(2)->reference_count() == 1,
        "closing the entire parent still preserves child objects"
    );
    child.close_all(fixture.pool);
    require(fixture.pool.in_use() == 0, "last child references release both objects");
}

void test_adopt_moves_references_without_retaining() {
    Fixture fixture;
    FileDescriptorTable source;
    FileDescriptorTable observer;
    FileDescriptorTable destination;
    for (int fd = 0; fd < 3; ++fd) {
        require_equal(source.open(fixture.pool, fixture.about()), fd, "prepare adopt source");
    }
    require_equal(source.close(fixture.pool, 1), 0, "adopt source contains a hole");
    require_equal(source.get(0)->commit_read(7), 0, "adopt source has a nonzero shared offset");
    require_equal(
        observer.inherit_from(fixture.pool, source),
        0,
        "retain a separate observation table"
    );
    const auto before = capture(source, fixture.pool);
    destination.adopt(source);
    require(source.empty(), "adopt empties the source table");
    require_equal(
        capture(destination, fixture.pool),
        before,
        "adopt preserves objects, fd holes, offsets and references"
    );
    destination.close_all(fixture.pool);
    require(
        observer.get(0)->reference_count() == 1 && observer.get(0)->offset() == 7,
        "closing moved references preserves independently inherited ones"
    );
    observer.close_all(fixture.pool);
    require_equal(fixture.pool.in_use(), std::size_t{0}, "adopt introduces no leaked references");
}

void test_pool_capacity_and_retry() {
    Fixture fixture;
    std::array<FileDescriptorTable, 5> owners{};
    FileDescriptorTable retry;
    std::array<OpenState, OpenFilePool::kCapacity> states{};
    for (auto& owner : owners) {
        for (int fd = 0; fd < static_cast<int>(FileDescriptorTable::kCapacity); ++fd) {
            require_equal(
                owner.open(fixture.pool, fixture.about()),
                fd,
                "five tables fill the forty-entry pool"
            );
        }
    }
    require_equal(
        owners[0].get(0)->commit_read(2),
        0,
        "retain a nonzero offset while pool is full"
    );
    for (std::size_t i = 0; i < states.size(); ++i) {
        states[i] = capture(owners[i / 8].get(static_cast<int>(i % 8)));
    }
    require_equal(
        fixture.pool.in_use(),
        OpenFilePool::kCapacity,
        "all forty pool objects are active"
    );
    require_equal(
        retry.open(fixture.pool, fixture.about()),
        -1,
        "pool exhaustion rejects another table's open"
    );
    require(
        retry.empty() && fixture.pool.in_use() == OpenFilePool::kCapacity,
        "pool failure leaves the destination empty and usage unchanged"
    );
    for (std::size_t i = 0; i < states.size(); ++i) {
        require_equal(
            capture(owners[i / 8].get(static_cast<int>(i % 8))),
            states[i],
            "pool failure preserves all existing positions and reference counts"
        );
    }
    const OpenFile* freed = owners[2].get(3);
    require_equal(owners[2].close(fixture.pool, 3), 0, "release one pool object");
    require_equal(
        retry.open(fixture.pool, fixture.about()),
        0,
        "open retries successfully after release"
    );
    require(
        retry.get(0) == freed && retry.get(0)->offset() == 0 &&
            retry.get(0)->reference_count() == 1,
        "retry reuses the free pool storage with fresh state"
    );
    for (auto& owner : owners) {
        owner.close_all(fixture.pool);
    }
    retry.close_all(fixture.pool);
    require_equal(
        fixture.pool.in_use(),
        std::size_t{0},
        "all forty objects are available after cleanup"
    );
    require(
        fixture.filesystem.lookup({kAboutPath, sizeof(kAboutPath) - 1}) != nullptr,
        "closing objects does not destroy the underlying file"
    );
}
} // namespace

int test_open_file() {
    auto run = [](const char* name, auto operation) {
        return run_case(name, [&] {
            const auto allocations = host_allocation_calls();
            const auto calls = host_call_counts();
            operation();
            require_equal(
                host_allocation_calls(),
                allocations,
                "open-file model performs no PMM allocation"
            );
            require_equal(
                host_call_counts(),
                calls,
                "open-file model performs no unmap, flush or free"
            );
        });
    };
    bool passed = true;
    passed = run("open file: prepare, commit, EOF and empty file",
                 test_read_prepare_commit_and_empty_file) &&
             passed;
    passed = run("open file: independent opens keep independent positions",
                 test_independent_opens_have_independent_positions) &&
             passed;
    passed = run("open file: descriptor bounds, full table and hole reuse",
                 test_descriptor_bounds_capacity_and_reuse) &&
             passed;
    passed = run("open file: inheritance shares positions and preserves fd holes",
                 test_inheritance_shares_objects_and_preserves_holes) &&
             passed;
    passed = run("open file: adopt transfers existing references without increment",
                 test_adopt_moves_references_without_retaining) &&
             passed;
    passed = run("open file: forty-entry exhaustion preserves state and supports retry",
                 test_pool_capacity_and_retry) &&
             passed;
    return passed ? 0 : -1;
}

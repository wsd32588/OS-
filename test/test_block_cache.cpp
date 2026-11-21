#include "storage/block_cache.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <initializer_list>
#include <utility>

#include "test_support.hpp"

namespace {

using tinyos::storage::BlockCache;
using tinyos_test::require;
using tinyos_test::require_equal;
using tinyos_test::run_case;

constexpr std::size_t kSectorSize = 512;
constexpr std::size_t kSectorCount = 9;
using Sector = std::array<unsigned char, kSectorSize>;

struct Access {
    char operation;
    std::uint64_t sector;
    int result;

    bool operator==(const Access&) const = default;
};

struct MemoryDisk {
    std::array<Sector, kSectorCount> blocks{};
    std::array<Access, 8> accesses{};
    std::size_t access_count{};
    bool fail_read{};
    bool fail_write{};

    void record(Access access) noexcept {
        if (access_count == accesses.size()) {
            std::abort();
        }
        accesses[access_count++] = access;
    }
};

// Cache callbacks have no context parameter. Each serial test starts with a fresh fake disk.
MemoryDisk g_disk{};

Sector sector_bytes(std::uint64_t seed) {
    Sector bytes{};
    for (std::size_t offset = 0; offset < bytes.size(); ++offset) {
        bytes[offset] = static_cast<unsigned char>((seed + offset) & 0xff);
    }
    return bytes;
}

void reset_disk() {
    g_disk = {};
    for (std::size_t sector = 0; sector < g_disk.blocks.size(); ++sector) {
        g_disk.blocks[sector] = sector_bytes(17 + sector * 53);
    }
}

int read_sector(std::uint64_t sector, std::span<unsigned char> destination) noexcept {
    const bool fail = std::exchange(g_disk.fail_read, false) ||
                      sector >= g_disk.blocks.size() || destination.size() != kSectorSize;
    g_disk.record({'R', sector, fail ? -1 : 0});
    if (fail) {
        return -1;
    }

    std::ranges::copy(g_disk.blocks[sector], destination.begin());
    return 0;
}

int write_sector(std::uint64_t sector, std::span<const unsigned char> source) noexcept {
    const bool fail = std::exchange(g_disk.fail_write, false) ||
                      sector >= g_disk.blocks.size() || source.size() != kSectorSize;
    g_disk.record({'W', sector, fail ? -1 : 0});
    if (fail) {
        return -1;
    }

    std::ranges::copy(source, g_disk.blocks[sector].begin());
    return 0;
}

void expect_trace(std::initializer_list<Access> expected, const char* message) {
    const std::span observed{g_disk.accesses.data(), g_disk.access_count};
    require(std::ranges::equal(observed, expected), message);
}

void expect_read(BlockCache& cache, std::uint64_t sector, const Sector& expected) {
    Sector destination{};
    require_equal(cache.read(sector, destination), 0, "cached read succeeds");
    require(destination == expected, "cached read returns the expected full sector");
}

void test_empty_read_hit_and_clean_replacement() {
    reset_disk();
    BlockCache cache{kSectorCount, {read_sector, write_sector}};

    require_equal(cache.flush(), 0, "flushing an empty cache succeeds");
    expect_trace({}, "flushing an empty cache does no I/O");
    expect_read(cache, 0, g_disk.blocks[0]);
    expect_trace({{'R', 0, 0}}, "sector zero loads into an initially invalid slot");
    expect_read(cache, 0, g_disk.blocks[0]);
    expect_trace({{'R', 0, 0}}, "the second read of sector zero hits the cache");
    expect_read(cache, 1, g_disk.blocks[1]);
    require_equal(cache.flush(), 0, "flushing a clean cache succeeds");
    expect_trace({{'R', 0, 0}, {'R', 1, 0}}, "clean replacement only reads the new sector");
}

void test_full_write_and_flush() {
    reset_disk();
    BlockCache cache{kSectorCount, {read_sector, write_sector}};
    const Sector original_seven = g_disk.blocks[7];
    const Sector original_eight = g_disk.blocks[8];
    const Sector new_seven = sector_bytes(0x60);
    const Sector new_eight = sector_bytes(0xa5);
    Sector source = sector_bytes(0x30);

    require_equal(cache.write(7, source), 0, "a full-sector write fills an empty slot");
    source = new_seven;
    require_equal(cache.write(7, source), 0, "a second write updates the same slot");
    source.fill(0xff);
    expect_read(cache, 7, new_seven);
    expect_trace({}, "full-sector writes skip pre-read and defer writeback");
    require(g_disk.blocks[7] == original_seven, "the disk still has the old sector before flush");

    require_equal(cache.flush(), 0, "flush writes the dirty slot");
    require_equal(cache.flush(), 0, "another flush succeeds on the now-clean slot");
    expect_read(cache, 7, new_seven);
    expect_trace({{'W', 7, 0}}, "one flush writes sector seven and leaves it cached");
    require(g_disk.blocks[7] == new_seven, "flush writes the last cached bytes");

    const Sector next_seven = sector_bytes(0x53);
    require_equal(cache.write(7, next_seven), 0, "sector seven becomes dirty again");
    require_equal(cache.write(8, new_eight), 0, "a full-sector write replaces the dirty slot");
    expect_read(cache, 8, new_eight);
    expect_trace({{'W', 7, 0}, {'W', 7, 0}}, "full write on a miss only writes back the old slot");
    require(g_disk.blocks[7] == next_seven && g_disk.blocks[8] == original_eight,
            "replacement writes the old slot's new data and keeps sector eight deferred");
}

void test_failed_replacement_and_retry() {
    reset_disk();
    BlockCache cache{kSectorCount, {read_sector, write_sector}};
    const Sector original_seven = g_disk.blocks[7];
    const Sector new_seven = sector_bytes(0x75);
    Sector destination{};
    destination.fill(0x5a);
    const Sector sentinel = destination;
    require_equal(cache.write(7, new_seven), 0, "make sector seven dirty without I/O");
    expect_trace({}, "preparing the dirty slot does no I/O");

    g_disk.fail_write = true;
    require_equal(cache.read(8, destination), -1, "failed writeback rejects replacement");
    require(destination == sentinel, "failed writeback leaves caller bytes alone");
    require(g_disk.blocks[7] == original_seven, "the fake device rejected writeback");
    expect_read(cache, 7, new_seven);
    expect_trace({{'W', 7, -1}}, "failed writeback never tries to load sector eight");

    g_disk.fail_read = true;
    require_equal(cache.read(8, destination), -1, "failed refill rejects replacement");
    require(destination == sentinel, "failed refill leaves caller bytes alone");
    require(g_disk.blocks[7] == new_seven, "the retry writes the retained dirty bytes");
    expect_read(cache, 7, new_seven);
    expect_trace({{'W', 7, -1}, {'W', 7, 0}, {'R', 8, -1}},
                 "failed refill leaves the now-clean old sector resident");

    expect_read(cache, 8, g_disk.blocks[8]);
    expect_trace({{'W', 7, -1}, {'W', 7, 0}, {'R', 8, -1}, {'R', 8, 0}},
                 "the final retry only reads the requested sector");
}

void test_bad_requests_preserve_the_slot() {
    reset_disk();
    BlockCache cache{kSectorCount, {read_sector, write_sector}};
    const Sector source = sector_bytes(0x31);
    Sector destination{};
    destination.fill(0x5a);
    const Sector sentinel = destination;

    require_equal(cache.write(7, source), 0, "create a resident dirty sector");
    require_equal(cache.read(8, std::span{destination}.first(511)), -1, "reject a short read");
    require_equal(cache.write(8, std::span{source}.first(511)), -1, "reject a short write");
    require_equal(cache.read(kSectorCount, destination), -1, "reject read at capacity");
    require_equal(cache.write(kSectorCount, source), -1, "reject write at capacity");
    require(destination == sentinel, "rejected reads preserve all destination bytes");
    expect_read(cache, 7, source);
    expect_trace({}, "invalid parameters neither evict nor write the dirty slot");
    require_equal(cache.flush(), 0, "the dirty slot can still be written back");
    expect_trace({{'W', 7, 0}}, "writeback still uses the original sector");

    BlockCache readonly_cache{kSectorCount, {read_sector, nullptr}};
    expect_read(readonly_cache, 0, g_disk.blocks[0]);
    require_equal(readonly_cache.write(1, source), -1, "read-only cache rejects writes");
    expect_read(readonly_cache, 0, g_disk.blocks[0]);
    expect_trace({{'W', 7, 0}, {'R', 0, 0}}, "read-only refusal retains the old clean slot");
}

}

int test_block_cache() {
    bool passed = true;
    passed = run_case("block cache: empty slot, hit and clean replacement",
                      test_empty_read_hit_and_clean_replacement) && passed;
    passed = run_case("block cache: full writes and explicit flush",
                      test_full_write_and_flush) && passed;
    passed = run_case("block cache: failed writeback and refill stay retryable",
                      test_failed_replacement_and_retry) && passed;
    passed = run_case("block cache: invalid and read-only requests preserve the slot",
                      test_bad_requests_preserve_the_slot) && passed;
    return passed ? 0 : -1;
}

#include "fs/builtin_fs.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "test_support.hpp"

namespace {
using namespace tinyos_test;
using tinyos::fs::BuiltinFile;
using tinyos::fs::BuiltinFileSystem;

constexpr unsigned char kGuard = 0xcc;
constexpr std::array<unsigned char, 5> kBinary{'A', 0, 0xff, 'Z', 0x80};
constexpr unsigned char kText[] = "TinyOS builtin fs\n";

template <std::size_t N>
std::span<const char> path(const char (&name)[N]) noexcept {
    return {name, N - 1};
}

void expect_rejected_init(std::span<const BuiltinFile> files) {
    BuiltinFileSystem fs;
    require(fs.lookup(path("/retry")) == nullptr, "uninitialized lookup returns null");
    require_equal(fs.init(files), -1, "invalid initialization rejects the whole input");
    for (const BuiltinFile& file : files) {
        require(fs.lookup(file.path) == nullptr, "rejected initialization publishes no prefix");
    }
    const BuiltinFile retry[] = {{path("/retry"), kBinary}};
    require_equal(fs.init(retry), 0, "valid configuration succeeds after a failed attempt");
    const BuiltinFile* found = fs.lookup(path("/retry"));
    require(found != nullptr && found->bytes.data() == kBinary.data() &&
            found->bytes.size() == kBinary.size(), "retry publishes the correct borrowed bytes");
}

void test_init_invalid_inputs_and_retry() {
    expect_rejected_init({});
    const std::array invalid_paths{
        std::span<const char>{}, path("/"), path("relative"), path("/bin/"),
        path("//bin"), path("/bin//demo"), path("/bin/./demo"),
        path("/bin/../demo"), path("/."), path("/.."), path("/bin/de\0mo")
    };
    for (const auto invalid_path : invalid_paths) {
        const BuiltinFile files[] = {{path("/valid"), kBinary}, {invalid_path, kBinary}};
        expect_rejected_init(files);
    }
}

void test_capacity_and_last_duplicate_are_transactional() {
    const std::array names{path("/a0"), path("/a1"), path("/a2"), path("/a3"),
                           path("/a4"), path("/a5"), path("/a6"), path("/a7"), path("/a8")};
    std::array<BuiltinFile, BuiltinFileSystem::kCapacity + 1> files{};
    for (std::size_t i = 0; i < files.size(); ++i) {
        files[i] = {names[i], kBinary};
    }
    expect_rejected_init(files);
    BuiltinFileSystem full;
    require_equal(full.init(std::span(files).first(BuiltinFileSystem::kCapacity)), 0,
                  "exactly eight unique files fit");
    for (std::size_t i = 0; i < BuiltinFileSystem::kCapacity; ++i) {
        require(full.lookup(names[i]) != nullptr, "every capacity slot can be found");
    }

    static constexpr char duplicate[] = "/a0";
    require(names[0].data() != duplicate, "duplicate path uses distinct underlying storage");
    files[BuiltinFileSystem::kCapacity - 1] = {path(duplicate), kBinary};
    expect_rejected_init(std::span(files).first(BuiltinFileSystem::kCapacity));
}

void test_descriptor_copy_reinit_and_empty_file() {
    std::array<BuiltinFile, 3> files{{
        {path("/bin/demo"), kBinary},
        {path("/bin/worker"), {kText, sizeof(kText) - 1}},
        {path("/empty"), {}}
    }};
    BuiltinFileSystem fs;
    require_equal(fs.init(files), 0, "initialize independent stored descriptors");
    const BuiltinFile* original = fs.lookup(path("/bin/demo"));
    require(original != nullptr && original != &files[0], "lookup borrows the filesystem record");
    files.fill({});
    require(fs.lookup(path("/bin/demo")) == original &&
            original->bytes.data() == kBinary.data() && original->bytes.size() == kBinary.size(),
            "overwriting input descriptors preserves copied descriptions and borrowed data");
    require(fs.lookup(path("/bin/worker")) != nullptr, "another copied descriptor survives");
    const BuiltinFile replacement[] = {{path("/replacement"), kBinary}};
    require_equal(fs.init(replacement), -1, "successful filesystem cannot be configured twice");
    require(fs.lookup(path("/bin/demo")) == original && fs.lookup(path("/replacement")) == nullptr,
            "repeat initialization preserves existing pointer identity and names");

    const BuiltinFile* empty = fs.lookup(path("/empty"));
    require(empty != nullptr && empty->bytes.empty(), "empty file is distinct from a missing name");
    std::array<unsigned char, 4> output{};
    output.fill(kGuard);
    const auto before = output;
    require_equal(empty->read_at(0, output), 0L, "empty file reads zero bytes");
    require_equal(output, before, "empty file does not touch the destination");
}

void test_lookup_exact_names_and_path_rules() {
    const BuiltinFile files[] = {
        {path("/bin/demo"), kBinary}, {path("/bin/worker"), kBinary},
        {path("/etc/about"), {kText, sizeof(kText) - 1}},
        {path("/.hidden"), {}}, {path("/bin/.hidden"), {}}
    };
    BuiltinFileSystem fs;
    require_equal(fs.init(files), 0, "hidden names are ordinary valid components");
    for (const BuiltinFile& file : files) {
        const BuiltinFile* found = fs.lookup(file.path);
        require(found != nullptr && found->bytes.data() == file.bytes.data() &&
                found->bytes.size() == file.bytes.size(), "exact full path finds its own file");
    }
    const std::array misses{
        path("/missing"), path("/bin"), path("/bin/dem"), path("/bin/demo/extra"),
        path("/BIN/demo"), path("bin/demo"), path(""), path("/"),
        path("/bin//demo"), path("/bin/demo/"), path("/bin/./demo"),
        path("/bin/../demo"), path("/bin/de\0mo")
    };
    for (const auto name : misses) {
        require(fs.lookup(name) == nullptr, "lookup rejects unknown or invalid full paths");
    }
}

void test_path_length_boundary() {
    std::array<char, tinyos::fs::kMaxBuiltinPathLength> longest{};
    longest.fill('x');
    longest[0] = '/';
    std::array<char, tinyos::fs::kMaxBuiltinPathLength + 1> too_long{};
    too_long.fill('x');
    too_long[0] = '/';
    const BuiltinFile valid[] = {{longest, kBinary}};
    BuiltinFileSystem fs;
    require_equal(fs.init(valid), 0, "127-byte path is accepted");
    require(fs.lookup(longest) != nullptr, "127-byte exact path can be found");
    require(fs.lookup(too_long) == nullptr, "128-byte lookup is rejected");
    const BuiltinFile invalid[] = {{path("/valid"), kBinary}, {too_long, kBinary}};
    expect_rejected_init(invalid);
}

void test_binary_reads_and_short_read_guards() {
    const BuiltinFile file{path("/binary"), kBinary};
    const auto original = kBinary;
    std::array<unsigned char, 8> output{};
    output.fill(kGuard);
    require_equal(file.read_at(0, output), static_cast<long>(kBinary.size()),
                  "binary read returns its actual short count");
    for (std::size_t i = 0; i < output.size(); ++i) {
        require_equal(output[i], i < kBinary.size() ? kBinary[i] : kGuard,
                      "binary zero/high-bit bytes and short-read tail are exact");
    }
    output.fill(kGuard);
    require_equal(file.read_at(1, std::span(output).subspan(1, 3)), 3L,
                  "offset read copies the requested binary slice");
    const std::array<unsigned char, 8> expected{kGuard, 0, 0xff, 'Z', kGuard, kGuard, kGuard, kGuard};
    require_equal(output, expected, "offset destination guards and exact copied bytes");
    require_equal(kBinary, original, "source binary bytes remain unchanged");
}

void test_text_segments_and_noop_ranges() {
    const BuiltinFile file{path("/text"), {kText, sizeof(kText) - 1}};
    std::array<unsigned char, sizeof(kText) - 1> assembled{};
    std::size_t offset = 0;
    while (offset < assembled.size()) {
        const std::size_t remaining = assembled.size() - offset;
        const auto count = remaining < 5 ? remaining : 5;
        const long read = file.read_at(offset, std::span(assembled).subspan(offset, count));
        require_equal(read, static_cast<long>(count), "segmented read returns the current count");
        offset += static_cast<std::size_t>(read);
    }
    for (std::size_t i = 0; i < assembled.size(); ++i) {
        require_equal(assembled[i], kText[i], "concatenated segments equal the original text");
    }
    std::array<unsigned char, 4> output{};
    output.fill(kGuard);
    const auto before = output;
    for (std::size_t position : {file.bytes.size(), file.bytes.size() + 1, SIZE_MAX}) {
        require_equal(file.read_at(position, output), 0L, "EOF and beyond return zero");
        require_equal(output, before, "EOF and extreme offsets leave guards untouched");
    }
    require_equal(file.read_at(0, {}), 0L, "empty destination at a readable offset returns zero");
    require_equal(file.read_at(SIZE_MAX, {}), 0L, "empty destination at SIZE_MAX returns zero");
    require(std::string_view(reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()) ==
            "TinyOS builtin fs\n", "all text reads preserve the borrowed source");
}
} // namespace

int test_builtin_fs() {
    bool passed = true;
    passed = run_case("builtin fs: rejected initialization stays empty and can retry",
                      test_init_invalid_inputs_and_retry) && passed;
    passed = run_case("builtin fs: capacity and final duplicate reject transactionally",
                      test_capacity_and_last_duplicate_are_transactional) && passed;
    passed = run_case("builtin fs: descriptions copy, pointers persist and empty files exist",
                      test_descriptor_copy_reinit_and_empty_file) && passed;
    passed = run_case("builtin fs: exact names, case and component validation",
                      test_lookup_exact_names_and_path_rules) && passed;
    passed = run_case("builtin fs: 127 and 128 byte path boundary",
                      test_path_length_boundary) && passed;
    passed = run_case("builtin fs: binary and offset reads preserve short-read guards",
                      test_binary_reads_and_short_read_guards) && passed;
    passed = run_case("builtin fs: segmented text and empty/EOF/extreme-offset reads",
                      test_text_segments_and_noop_ranges) && passed;
    return passed ? 0 : -1;
}

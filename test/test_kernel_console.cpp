#include "debug/console.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

#include "debug/file_view.hpp"
#include "fs/builtin_fs.hpp"
#include "fs/file_service.hpp"
#include "user_memory_fixture.hpp"

namespace {
using namespace tinyos_test;
using tinyos::debug::ConsoleOutput;
using tinyos::debug::KernelConsole;
using tinyos::debug::cat_file;
using tinyos::debug::hex_file;
using tinyos::fs::BuiltinFile;

constexpr std::string_view kPrompt = "kdbg> ";
constexpr std::string_view kHelp = "help\ncat PATH\nhex PATH (first 64 bytes)\n";
constexpr std::string_view kAbout = "TinyOS builtin fs\n";
constexpr std::string_view kAboutHex =
    "00000000: 54 69 6e 79 4f 53 20 62 75 69 6c 74 69 6e 20 66\n"
    "00000010: 73 0a\n";

struct Capture {
    std::array<char, 8192> bytes{};
    std::array<size_t, 1024> chunks{};
    size_t length{};
    size_t chunk_count{};
    bool overflow{};

    // Copy synchronously; assertions remain outside the noexcept callback.
    static void write(void* context, std::span<const char> source) noexcept {
        auto& capture = *static_cast<Capture*>(context);
        if (capture.chunk_count >= capture.chunks.size() ||
            source.size() > capture.bytes.size() - capture.length) {
            capture.overflow = true;
            return;
        }
        capture.chunks[capture.chunk_count++] = source.size();
        for (char byte : source) {
            capture.bytes[capture.length++] = byte;
        }
    }

    ConsoleOutput output() { return {this, write}; }
    std::string text() const {
        require(!overflow, "console capture capacity covers the complete synchronous output");
        return {bytes.data(), length};
    }
};

const BuiltinFile& lookup(std::string_view path) {
    const auto* file = tinyos::fs::lookup_file({path.data(), path.size()});
    require(file != nullptr, "use the permanent provider configured by earlier syscall tests");
    return *file;
}

void feed(KernelConsole& console, std::string_view bytes) {
    for (unsigned char byte : bytes) {
        console.feed(byte);
    }
}

void test_cat_binary_chunks_and_empty_file() {
    std::array<unsigned char, 65> source{};
    for (size_t i = 0; i < source.size(); ++i) {
        source[i] = static_cast<unsigned char>(i + 1);
    }
    source[0] = 0;
    source[1] = 0xff;
    const auto original = source;
    constexpr char path[] = "/binary";
    const BuiltinFile file{{path, sizeof(path) - 1}, source};
    const auto descriptor = file;
    Capture capture;
    require_equal(cat_file(file, capture.output()), 0, "cat succeeds for a 65-byte binary file");
    require_equal(capture.text(), std::string(reinterpret_cast<const char*>(source.data()), source.size()),
                  "cat synchronously copies every byte including NUL and ff without adding a newline");
    require(capture.chunk_count == 2 && capture.chunks[0] == 64 && capture.chunks[1] == 1,
            "cat emits actual read counts as 64 plus 1 bytes");
    require(source == original && file.path.data() == descriptor.path.data() &&
            file.path.size() == descriptor.path.size() && file.bytes.data() == descriptor.bytes.data() &&
            file.bytes.size() == descriptor.bytes.size(), "cat preserves source storage and descriptor");
    Capture empty;
    require_equal(cat_file(lookup("/empty"), empty.output()), 0, "empty cat is successful");
    require_equal(hex_file(lookup("/empty"), empty.output()), 0, "empty hex is successful");
    require(empty.text().empty() && empty.chunk_count == 0, "empty files produce no data callbacks");
}

void test_hex_short_tail_and_preview_limit() {
    Capture about;
    require_equal(hex_file(lookup("/etc/about"), about.output()), 0, "hex reads the registered about file");
    require_equal(about.text(), std::string(kAboutHex), "hex has exact offsets, spacing, short tail and LF");
    require(about.chunk_count == 2 && about.chunks[0] == 58 && about.chunks[1] == 16,
            "hex emits a complete sixteen-byte row and a two-byte row");
    std::array<unsigned char, 65> source{};
    source.fill('A');
    source[0] = 0;
    source[1] = 0xff;
    source[64] = 'Z';
    const auto original = source;
    constexpr char path[] = "/binary";
    const BuiltinFile file{{path, sizeof(path) - 1}, source};
    Capture preview;
    require_equal(hex_file(file, preview.output()), 0, "hex accepts a file longer than its preview");
    const std::string expected =
        "00000000: 00 ff 41 41 41 41 41 41 41 41 41 41 41 41 41 41\n"
        "00000010: 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41\n"
        "00000020: 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41\n"
        "00000030: 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41\n";
    require_equal(preview.text(), expected, "hex prints exactly the first 64 bytes and omits byte 65");
    require(preview.chunk_count == 4 && source == original,
            "four-row preview preserves all source bytes");
}

void test_commands_and_argument_errors() {
    Capture capture;
    KernelConsole console{capture.output()};
    console.start();
    std::string expected(kPrompt);
    const auto command = [&](std::string_view input, std::string_view output) {
        feed(console, input);
        console.feed('\n');
        expected += input;
        expected += '\n';
        expected += output;
        expected += kPrompt;
    };
    command("help", kHelp);
    command("  cat   /etc/about  ", kAbout);
    command("hex /etc/about", kAboutHex);
    command("cat /empty", "");
    command("hex /empty", "");
    command("", "");
    command("    ", "");
    command("help extra", "error: invalid arguments\n");
    command("cat", "error: invalid arguments\n");
    command("hex /etc/about extra", "error: invalid arguments\n");
    command("wat", "error: unknown command\n");
    command("cat /missing", "error: file not found\n");
    command("hex /etc/../about", "error: file not found\n");
    require_equal(capture.text(), expected, "commands, empty lines, spaces and errors preserve exact prompts");
}

void test_line_endings_and_backspace_delete() {
    Capture capture;
    KernelConsole console{capture.output()};
    console.start();
    feed(console, "\r\nhelp\rcat /etc/about\n\r\r\n");
    feed(console, "\b\x7fhelx\bp\n");
    feed(console, "hexX\x7f /etc/about\n");
    const std::string expected = std::string(kPrompt) + "\n" + std::string(kPrompt) +
        "help\n" + std::string(kHelp) + std::string(kPrompt) +
        "cat /etc/about\n" + std::string(kAbout) + std::string(kPrompt) +
        "\n" + std::string(kPrompt) + "\n" + std::string(kPrompt) +
        "helx\b \bp\n" + std::string(kHelp) + std::string(kPrompt) +
        "hexX\b \b /etc/about\n" + std::string(kAboutHex) + std::string(kPrompt);
    require_equal(capture.text(), expected,
                  "CR/LF/CRLF commit once, CR non-LF resumes input, BS/DEL edit and ignore empty input");
}

void test_capacity_rejection_and_recovery() {
    Capture capture;
    KernelConsole console{capture.output()};
    console.start();
    const std::string valid = "help" + std::string(KernelConsole::kLineCapacity - 4, ' ');
    require_equal(valid.size(), size_t{160}, "prepare an exactly 160-character valid command");
    feed(console, valid);
    console.feed('\n');
    feed(console, valid);
    console.feed(' ');
    feed(console, "\bignored\x7f\r\nhelp\n");
    const std::string expected = std::string(kPrompt) + valid + "\n" + std::string(kHelp) +
        std::string(kPrompt) + valid + "\nerror: line rejected\n" + std::string(kPrompt) +
        "help\n" + std::string(kHelp) + std::string(kPrompt);
    require_equal(capture.text(), expected,
                  "160 is accepted; byte 161 discards the whole line, editing cannot restore it, next line retries");
}

void test_invalid_bytes_discard_whole_line() {
    for (unsigned char invalid : std::array<unsigned char, 3>{0, 0xff, '\t'}) {
        Capture capture;
        KernelConsole console{capture.output()};
        console.start();
        feed(console, "help");
        console.feed(invalid);
        feed(console, "\bignored\x7f\r\nhelp\n");
        const std::string expected = std::string(kPrompt) + "help\nerror: line rejected\n" +
            std::string(kPrompt) + "help\n" + std::string(kHelp) + std::string(kPrompt);
        require_equal(capture.text(), expected,
                      "NUL/ff/TAB reject the entire line without echo or recovery by BS; CRLF and retry work");
    }
}

bool run_checked(const char* name, void (*test)()) {
    return run_case(name, [test] {
        PagePool<1> pages;
        const auto allocations = host_allocation_calls();
        const auto calls = host_call_counts();
        const auto available = pmm_available_pages();
        const auto& about = lookup("/etc/about");
        const auto& empty = lookup("/empty");
        const auto about_descriptor = about;
        const auto empty_descriptor = empty;
        std::array<unsigned char, 18> original{};
        std::copy(about.bytes.begin(), about.bytes.end(), original.begin());
        test();
        require_equal(host_allocation_calls(), allocations, "console and file views allocate no physical pages");
        require_equal(host_call_counts(), calls, "console and file views do not release, unmap or flush pages");
        require_equal(pmm_available_pages(), available, "console and file views preserve available pages");
        require(about.path.data() == about_descriptor.path.data() && about.path.size() == about_descriptor.path.size() &&
                about.bytes.data() == about_descriptor.bytes.data() && about.bytes.size() == about_descriptor.bytes.size() &&
                empty.path.data() == empty_descriptor.path.data() && empty.path.size() == empty_descriptor.path.size() &&
                empty.bytes.data() == empty_descriptor.bytes.data() && empty.bytes.size() == empty_descriptor.bytes.size() &&
                std::equal(about.bytes.begin(), about.bytes.end(), original.begin()),
                "console operations preserve permanent file descriptors and borrowed source bytes");
    });
}
}

int test_kernel_console() {
    bool passed = true;
    passed = run_checked("file view: cat binary 64+1 chunks and empty success", test_cat_binary_chunks_and_empty_file) && passed;
    passed = run_checked("file view: hex exact short tail and first-64-byte limit", test_hex_short_tail_and_preview_limit) && passed;
    passed = run_checked("kernel console: commands, whitespace and parse errors", test_commands_and_argument_errors) && passed;
    passed = run_checked("kernel console: line endings and BS/DEL editing", test_line_endings_and_backspace_delete) && passed;
    passed = run_checked("kernel console: 160/161 boundary, discard and retry", test_capacity_rejection_and_recovery) && passed;
    passed = run_checked("kernel console: invalid bytes discard whole lines and recover", test_invalid_bytes_discard_whole_line) && passed;
    return passed ? 0 : -1;
}

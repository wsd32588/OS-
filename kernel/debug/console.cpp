#include "debug/console.hpp"
#include "debug/file_view.hpp"
#include "fs/file_service.hpp"

namespace tinyos::debug {
struct BuiltinFile;

void (*write)(void*, std::span<const char>) noexcept{};

namespace  {
bool token_equals(std::span<const char> token, const char* text) noexcept {
    size_t text_length = 0;
    while (text[text_length] != '\0') {
        ++text_length;
    }
    if (token.size() != text_length) {
        return false;
    }
    for (size_t i = 0; i < token.size(); ++i) {
        if (token[i] != text[i]) {
            return false;
        }
    }
    return true;
}
}

void KernelConsole::write_text(
    std::span<const char> bytes
) const noexcept {
    output_.write(output_.context, bytes);
}

void KernelConsole::start() noexcept {
    constexpr char kPrompt[] = "kdbg> ";
    write_text({kPrompt, sizeof(kPrompt) - 1});
}


void KernelConsole::finish_line() noexcept {
    const char newline = '\n';
    write_text({&newline, 1});

    if (discard_line_) {
        constexpr char kTooLong[] = "error: line rejected\n";
        write_text({kTooLong, sizeof(kTooLong) - 1});
    } else {
        execute_line({line_, length_});
    }

    length_ = 0;
    discard_line_ = false;
    skip_lf_ = false;
    start();
}
void KernelConsole::execute_line(
    std::span<const char> line
) noexcept {
    std::span<const char> tokens[2]{};
    size_t token_count{};
    size_t i{};

    while(i < line.size()) {
        while (i < line.size() && line[i] == ' ') {
            ++i;
        }
        
        if (i == line.size()) {
            break;
        }

        const size_t start = i;
        while (i < line.size() && line[i] != ' ') {
            ++i;
        }

        if (token_count < 2) {
            tokens[token_count] = line.subspan(start, i - start);
        }
        ++token_count;

        if (token_count > 2) {
            break;
        }
    }

    if (token_count == 0) {
        return;
    }

    //判断分支，提前确认命令类型
    const bool is_help = token_equals(tokens[0], "help");
    const bool is_cat = token_equals(tokens[0], "cat");
    const bool is_hex = token_equals(tokens[0], "hex");

    if (!is_help && !is_cat && !is_hex) {
        constexpr char kMessage[] = "error: unknown command\n";
        write_text({kMessage, sizeof(kMessage) - 1});
        return;
    }

    const size_t expected_count = is_help ? 1 : 2;//使用help的时候只接收1个命令
    if (token_count != expected_count) {
        constexpr char kMessage[] = "error: invalid arguments\n";
        write_text({kMessage, sizeof(kMessage) - 1});
        return;
    }

    if (is_help) { // 输出可操作方式
        constexpr char kHelp[] =
            "help\n"
            "cat PATH\n"
            "hex PATH (first 64 bytes)\n";
        write_text({kHelp, sizeof(kHelp) - 1});
        return;
    }

    const fs::BuiltinFile* file = fs::lookup_file(tokens[1]);
    if (file == nullptr) {
        constexpr char kMessage[] = "error: file not found\n";
        write_text({kMessage, sizeof(kMessage) - 1});
        return;
    }

    const int result = is_cat
        ? cat_file(*file, output_)
        : hex_file(*file, output_);

    if (result < 0) {
        constexpr char kMessage[] = "error: file read failed\n";
        write_text({kMessage, sizeof(kMessage) - 1});
    }

}
void KernelConsole::feed(
    unsigned char byte
) noexcept {
    if (skip_lf_) {
        skip_lf_ = false;
        if (byte == '\n') {
            return;
        }
    }

    if (byte == '\n' ||
        byte == '\r') {
        finish_line();
        skip_lf_ = (byte == '\r');
        return;
    }

    if (discard_line_) {
        return;
    }

    if (byte == '\b' || byte == 0x7f) {
        if (discard_line_ || length_ == 0) {
            return;
        }
        --length_;
        const char erase[] = {'\b', ' ', '\b'};
        write_text({erase, sizeof(erase)});
        return;
    }

    if (byte < 0x20 || byte > 0x7e) {
        discard_line_ = true;
        return;
    }

    if (length_ + 1 > kLineCapacity) {
        discard_line_ = true;
        return;
    }
    line_[length_++] = static_cast<char>(byte);
    const char echo = static_cast<char>(byte);
    write_text({&echo, 1});
}
}
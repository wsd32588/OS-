#include "debug/file_view.hpp"

#include "fs/builtin_fs.hpp"
namespace tinyos::debug {
int cat_file(
    const fs::BuiltinFile& file, const ConsoleOutput& output
) noexcept {
    unsigned char buffer[64]{};
    size_t offset = 0;
    for (;;){
        long count = file.read_at(offset, buffer);
        if (count < 0) {
            return -1;
        } else if (count == 0) {
            return 0;
        }

        output.write(
            output.context,
            {reinterpret_cast<const char*>(buffer),
            static_cast<size_t>(count)});
        offset += static_cast<size_t>(count);
    }
}

int hex_file(
    const fs::BuiltinFile& file, const ConsoleOutput& output
) noexcept {
    unsigned char buffer[16]{};
    size_t offset{};

    for (size_t i = 0; i < 4; ++i) {
        const long count = file.read_at(offset, buffer);
        if (count < 0) {
            return -1;
        }
        if (count == 0) {
            return 0;
        }

        // 建立字符集
        constexpr char kDigits[] = "0123456789abcdef"; 
        char text[64]{};
        size_t length{};

        // 八位文件偏移。
        for (int shift = 28; shift >= 0; shift -= 4) {
            text[length++] = kDigits[(offset >> shift) & 0xfU];
        }
        text[length++] = ':';
        text[length++] = ' ';

        // 每个原始字节转换成两个可显示字符。
        for (size_t j = 0; j < static_cast<size_t>(count); ++j) {
            if (j != 0) {
                text[length++] = ' ';
            }
            text[length++] = kDigits[buffer[j] >> 4];
            text[length++] = kDigits[buffer[j] & 0xfU];
        }

        text[length++] = '\n'; // 末尾换行
        output.write(output.context, {text, length});
        offset += static_cast<size_t>(count);
    }

        return 0;
}

}

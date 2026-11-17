#ifndef TINYOS_DEBUG_FILE_VIEW_HPP
#define TINYOS_DEBUG_FILE_VIEW_HPP
#include "debug/console_output.hpp"
namespace tinyos::fs {
struct BuiltinFile;
}
namespace tinyos::debug {
int cat_file(const fs::BuiltinFile& file, const ConsoleOutput& output) noexcept;
int hex_file(const fs::BuiltinFile& file, const ConsoleOutput& output) noexcept;
}


#endif

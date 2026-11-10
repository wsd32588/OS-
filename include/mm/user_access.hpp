#pragma once

#include <span>
#include "mm/address.hpp"
#include "mm/vm.h"

namespace tinyos {
int copy_readable_user (
    const Sv39PageTable& root,
    VirtAddr source,
    std::span<unsigned char> destination
);

int copy_writable_user(
    const Sv39PageTable& root,
    VirtAddr destination,
    std::span<const unsigned char> source
);
}
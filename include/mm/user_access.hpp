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
}
#include "storage/block_cache.hpp"
#include "drivers/virtio_block.hpp"

namespace tinyos::storage {
int BlockCache::flush() noexcept {
    if (!dirty_) {
        return 0;
    }

    if (io_.write(sector_,
            std::span<const unsigned char>{data_, kSectorSize}) != 0) {
        return -1;
    }

    dirty_ = false;
    return 0;
}

int BlockCache::read(
    std::uint64_t sector,
    std::span<unsigned char> destination
) noexcept {
    if (destination.size() != kSectorSize ||
        sector >= sector_count_) {
        return -1;
    }

    if (!valid_ || sector_ != sector) {
        if (flush() < 0) {
            return -1;
        }
        if(io_.read(sector,
            std::span<unsigned char>{data_, kSectorSize}) != 0) {
            return -1;
        }
    }

    for (size_t i = 0; i < destination.size(); ++i) {
        destination[i] = data_[i];
    }

    sector_ = sector;
    valid_ = true;

    return 0;
}

int BlockCache::write(
    std::uint64_t sector,
    std::span<const unsigned char> source
) noexcept {
    if (source.size()  !=  kSectorSize ||
        io_.write == nullptr ||
        sector >= sector_count_) {
        return -1;
    }

    if (!valid_ || sector_ != sector) {
        if (flush() < 0) {
            return -1;
        }
    }

    for(size_t i = 0; i < source.size(); ++i) {
        data_[i] = source[i];
    }

    valid_ = true;
    dirty_ = true;
    sector_ = sector;

    return 0;
}
}

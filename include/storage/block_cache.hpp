#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace tinyos::storage{

struct BlockCacheIO {
    int (*read)(std::uint64_t, std::span<unsigned char>) noexcept;
    int (*write)(std::uint64_t, std::span<const unsigned char>) noexcept;
};

class BlockCache {
public:
    constexpr BlockCache(std::uint64_t sector_count, BlockCacheIO io) noexcept
        : sector_count_(sector_count), io_(io) {};
    BlockCache(const BlockCache&) = delete;
    BlockCache& operator=(const BlockCache&) = delete;
    BlockCache(BlockCache&&) = delete;
    BlockCache& operator=(BlockCache&&) = delete;

    int read(std::uint64_t sector, std::span<unsigned char> destination) noexcept;
    int write(std::uint64_t sector, std::span<const unsigned char> source) noexcept;
    int flush() noexcept;

private:
    static constexpr std::size_t kSectorSize = 512;
    std::uint64_t sector_count_{};
    BlockCacheIO io_{};
    std::uint64_t sector_{};
    bool valid_{};
    bool dirty_{};
    unsigned char data_[kSectorSize]{};
};
}

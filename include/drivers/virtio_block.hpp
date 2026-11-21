#ifndef TINYOS_DRIVERS_VIRTIO_BLOCK_HPP
#define TINYOS_DRIVERS_VIRTIO_BLOCK_HPP
#include <span>
#include <cstdint>

namespace tinyos::drivers {
int virtio_block_init(
    std::uintptr_t mmio_base, std::uint64_t& sector_count) noexcept;

int virtio_block_read(
    std::uint64_t sector,
    std::span<unsigned char> destination
) noexcept;

int virtio_block_write(
    std::uint64_t sector,
    std::span<const unsigned char> source
) noexcept;
}


#endif